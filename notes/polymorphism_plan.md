# Implementation Plan: `$T` / `$N` Compile-Time Polymorphic Parameters

A monomorphization-based implementation of polymorphic procedures for the Odin
compiler. Per call-site, type variables in the procedure signature are bound
to concrete types and integer constants from the argument expressions; the proc
AST is walked with a poly-variable environment stack (no deep clone) pushing
bindings onto `SemContext`; the resulting specialization is analyzed &
codegen'd as a normal procedure. A cache keyed on
`(proc symbol, arg type pointer tuple)` ensures only one specialization per
unique binding.

## Core Strategy: Env Stack (No AST Clone)

The original plan called for deep-cloning the AST with substitutions. Instead,
we use a **poly-aware env stack**:

1. **Detection** (unchanged): `poly_signature_is_polymorphic` — walk signature
   for any `AST_NODE_POLY_IDENT`.

2. **Instantiation at call-site**: build a `PolyEnv` that maps poly variable
   names (`"T"`, `"N"`) to concrete types/values. Push this env onto a stack
   in `SemContext`. Run `sem_analyse_procedure_literal` on the **original**
   (un-cloned) AST with `ctx->currently_instantiating = true`. Inside
   `sem_resolve_type_expr` and `sem_resolve_type_identifier`, consult the poly
   env stack. When env is active, `$T`/`T` resolves to the bound concrete type.
   After analysis, pop the env. Register the specialization symbol with a
   mangled name.

3. **Codegen** (unchanged from original plan): specialization symbols are
   enqueued in `ctx->pending_specializations`; `ir_generate()` drains them
   after the main top-level pass. The original poly symbol is skipped in codegen.

Benefits: ~500 LoC vs ~1200 LoC for deep-clone approach, no AST cloning bugs,
no need to deep-copy or reset `resolved_type`/`resolved_symbol` fields.

## Confirmed Scope

User decisions taken at plan time:

- **Both syntactic forms** — `proc(x: $T)` (shorthand) and `proc($T: typeid, x: T)` (explicit, deferred)
- **Specialization cache key** = `(symbol_t*, type-pointer-tuple)` hash
- **`where` clauses** — skipped initially (parse + ignore for now)
- **Overload bundles** — polymorphic candidates supported in bundles from start
  (Stage 6)
- **Plan granularity** — file-level plan + categorized tests

## Deferred Features (Not Supported Yet)

The following features are **explicitly out of scope** for the initial
implementation. They are listed here so we can address them in follow-up
stages. Tests for these features should be marked as EXPECTED-TO-FAIL or
skipped in the test list.

1. **`where` clauses** (`proc($T: $T) where type_of($T) == int`) — parsed
   today, completely ignored by the analyser. `where` enforcement needs:
   - Compile-time `type_of($T)` evaluation in a polymorphic context
   - Type-equality / constraint-language evaluation
   - Filtering of specializations against bound types
   - Error reporting for failed constraints
   **Plan**: Replace the "ignore" stub with a constraint-evaluator in a
   follow-up commit.

2. **Nested polymorphism** — ✅ DONE (Stage 8). A polymorphic proc can now call
   another polymorphic proc from its body. The poly env stack is searched top-down,
   so inner bindings are found first. The `currently_instantiating` flag is saved/
   restored across nested `poly_resolve_call` invocations.

3. **Forward declarations of polymorphic procs** — `---` bodyless syntax for
   poly procs (declaration + later definition). Tests avoid this; both the
   forward decl and the definition register a poly symbol today (no dedupe).

4. **Polymorphic struct / custom type definitions** — `Foo :: struct($N: int, T: $T) { ... }`
   where the struct itself is parameterized. **Plan**: distinct milestone
   extending poly detection into `AST_NODE_STRUCT_TYPE` and a separate cache
   on the type descriptor. Not in this plan.

5. **Cross-package polymorphic procs** — `import using` copies symbols but
   does not preserve `poly_origin_ast` (the AST living in an imported
   package that may already have been codegen'd). Tests are kept single-package
   in the initial implementation. **Plan**: hoist the original AST into a
   non-global AST arena owned by `SemContext` (package-independent) so it is
   visible to importers.

6. **Polymorphic return-type inference via `auto_cast` / untyped literals**
   — for `proc($T: $T, x: T) -> $T { return 0 }`, the literal `0` cannot be
   the basis for specializing `$T` unless there is at least one parameter
   using `$T` as its type. Tests only instantiate via parameters that bind
   `$T` to a concrete type.

7. **Explicit `$T: typeid` or `$N: int` parameter declarations** — the
   `(PolyIdent Colon)?` prefix in the `Parameter` grammar rule is present but
   does not work with the current grammar (because after `PolyIdent Colon`, the
   grammar expects `Identifier Colon TypePrefix`, which cannot be satisfied by
   `$T: typeid` since `typeid` is a reserved word). Only the shorthand form
   `x: $T` is supported initially.

## Pre-existing Bug Fixes (Applied)

### Scope UAFs (Fix applied during Stage 4)

Adding pointer-type fields to `symbol_t` (e.g. `poly_origin_ast`, `poly_cache`)
shifts the struct's memory layout and exposed pre-existing use-after-free bugs:

1. **`scope_lists.c` line 66**: `free(existing->name)` freed the hash-table
   entry's key pointer while the key is still in the hash table. This
   invalidated the key for future lookups.
   **Fix**: Use `strdup`d copy for the hash key.

2. **Scope-free ordering**: `scope_free` freed `symbol_t` block memory
   while AST nodes still held `node->resolved_symbol` pointers into freed
   memory.
   **Fix**: `generator_pop_scope` defers `scope_free`; `generator_free_deferred_scopes`
   is called from `main.c:598-600` after codegen completes.

### Polymorphic Call Codegen Dispatch (Fixed during Stage 4)

- **Root cause**: `ir_gen_postfix_call` in `ir_gen_postfix.c:167-216` used
  `sym->value.type_info` (the **original** polymorphic type with 0 runtime
  params — `$T: typeid` and `x: T` both skipped during pass1). The
  `resolved_symbol` path was gated behind
  `(proc_type == NULL || proc_type->kind != TD_KIND_PROC)`, which was false
  because the original type IS `TD_KIND_PROC`. The specialization's concrete
  type was never consulted for call codegen.
- **Fix**: Restructured the dispatch into 3 priorities:
  1. `op->resolved_symbol` from semantic analyser (`poly_resolve_call`)
  2. scope-based `sym` lookup
  3. `*cur_type` fallback (function pointers)
  When the semantic analyser has resolved a polymorphic call to a concrete
  specialization, that specialization's type and LLVM function value are used
  directly.

## Architecture Overview

Pipeline mirrors the existing semantic / IR pipeline:

1. **Detection (semantic pass 1)** — when registering a `ConstantDecl`
   whose value is a `ProcedureLiteral`, scan its signature for
   `AST_NODE_POLY_IDENT`. If found, mark the symbol as polymorphic and
   **skip body analysis**; register the proc symbol with `type_info = NULL`.
   Store the origin `ConstantDecl` node in a side table keyed by `symbol_t*`.

2. **Instantiation (semantic, at call-site)** — when
   `sem_evaluate_postfix_call` encounters a callee symbol marked polymorphic:
   - Evaluate argument types
   - Unify arg types with poly params to build an instantiation environment
   - Push env onto `SemContext` poly env stack; set `currently_instantiating = true`
   - Run `sem_analyse_procedure_literal` on the **original** `ProcedureDefinition`
     — this resolves the signature (POLY_IDENT nodes consult the env stack)
     and analyzes the body with concrete types
   - Pop env; set `currently_instantiating = false`
   - Register the specialization as a top-level symbol with mangled name
   - Add to `pending_specializations` for codegen

3. **Codegen** — specialization symbols are stored in
   `ctx->pending_specializations`. After each file's main semantic pass, the
   driver drains the queue and codegen's each as if it were a regular top-level
   procedure. Mangling: `<proc_name>__poly_<argtypecanon1>_<argtypecanon2>...`.

Polymorphism-specific code lives in `polymorphism.h` / `polymorphism.c`.

## New / Modified Modules

### 1. `src/polymorphism.h` / `src/polymorphism.c`

Public API:

```c
// --- Poly env (type-level only for Stage 3) ---
typedef enum { POLY_SLOT_TYPE, POLY_SLOT_INT } PolySlotKind;

#define MAX_POLY_ENV_ENTRIES 16
#define MAX_POLY_STACK_DEPTH 8

typedef struct {
    char const * name;              // "T", "N", ...
    PolySlotKind kind;
    TypeDescriptor const * bound_type;    // for POLY_SLOT_TYPE
    long long bound_int_value;           // for POLY_SLOT_INT
} PolyEnvEntry;

typedef struct {
    PolyEnvEntry entries[MAX_POLY_ENV_ENTRIES];
    int count;
} PolyEnv;

// Env stack management (SemContext owns the stack array)
void poly_env_push(SemContext * ctx, PolyEnv * env);
void poly_env_pop(SemContext * ctx);
TypeDescriptor const * poly_env_lookup_type(SemContext * ctx, char const * name);
bool poly_env_lookup_int(SemContext * ctx, char const * name, long long * out_val);

// Origin tracking — side table mapping symbol_t* → ConstantDecl AST node
void poly_register_origin(symbol_t * sym, odin_grammar_node_t * const_decl);
odin_grammar_node_t * poly_get_origin(symbol_t * sym);

// Instantiation result
typedef struct {
    symbol_t * symbol;       // the specialization symbol (with concrete type_info)
} PolySpecialization;

// Resolve a call to a polymorphic procedure.
PolySpecialization * poly_resolve_call(
    SemContext * ctx,
    symbol_t * poly_symbol,
    odin_grammar_node_t * call_op,
    odin_grammar_node_t * arg_list_node
);
```

### 2. `src/semantic_analyser.h`

Add to `SemContext`:

```c
// Poly env stack (env-stack approach — no AST clone)
PolyEnv poly_env_stack[MAX_POLY_STACK_DEPTH];
int poly_env_stack_depth;

// Pending specializations for codegen
PolySpecialization ** pending_specializations;
int pending_spec_count;
int pending_spec_capacity;
```

### 3. `src/sem_context.c`

Initialize new fields to zero in `sem_context_init`; free pending specs array
in `sem_context_destroy`.

### 4. `src/sem_type_resolver.c`

- Add `AST_NODE_POLY_IDENT` to the dispatch table → `sem_resolve_poly_ident_type`
  handler that consults `poly_env_lookup_type()`.
- Modify `sem_resolve_type_identifier` to also check the poly env stack
  (poly var names like `T` appear as bare `Identifier` in return types).

### 5. `src/sem_evaluate_expr.c`

In both `AST_NODE_POSTFIX_CALL` handlers (package-qualified and local):
- Before the existing `type->kind == TD_KIND_PROC` test, check if the callee
  symbol is polymorphic → route to `poly_resolve_call()`.
- Evaluate args, pass them to `poly_resolve_call()`, set
  `op->resolved_symbol` and `op->resolved_type` from the specialization.

### 6. `src/semantic_analyser.c`

- `sem_register_top_level_declaration`: after marking `sym->is_polymorphic`,
  call `poly_register_origin(sym, node)` (the ConstantDecl).
- `sem_resolve_procedure_signature`: when looking for `param_type_node`,
  also accept `AST_NODE_POLY_IDENT` alongside `Identifier` / `is_type_node`.

### 7. `src/llvm_ir_generator.c` / `src/ir_gen_postfix.c`

- `ir_gen_top_level_decl`: skip poly-origin symbols (already done by
  `ir_gen_postfix.c` error guard in Stage 2).
- `ir_generate()`: after the main top-level pass, drain
  `pending_specializations` and codegen each via the standard top-level path
  with mangled name.
- `ir_gen_postfix_call`: Priority 1 dispatch on `op->resolved_symbol`.
  Types the call using the specialization's concrete proc type and LLVM
  function value directly.

### 8. `src/symbols.h`

Only `bool is_polymorphic` (no pointer fields — UAF workaround).

## Semantic Details

### Polymorphic Param Detection
1. Walk `sig_node->children` for `PARAMETER_LIST` → `PARAMETERS` → `PARAMETER[]`.
2. For each `PARAMETER`:
   - Scan children for `AST_NODE_POLY_IDENT` in type position.
3. Walk `Returns` for the same poly uses (both `PolyIdent` and bare `Identifier`).
4. If any `PolyIdent` is found in params OR returns → return true.

### Instantiation Environment
- Initialize empty `PolyEnv`.
- For each parameter whose type is a `PolyIdent`:
  - Look up arg type at same position from call site.
  - If poly name already bound: verify type equality. Otherwise: add to env.
- Push env onto `SemContext` poly env stack.

### Env Resolution During Instantiation
- `sem_resolve_type_expr` for `AST_NODE_POLY_IDENT`: consult `poly_env_lookup_type()`.
- `sem_resolve_type_identifier` (bare `Identifier` like `T`): also consult
  `poly_env_lookup_type()` before falling through to scope lookup.

### Registration of Specializations
- Mangling: `<origin_name>__poly_<param_type1>_<param_type2>...`
- Use `type_write_canonical_name` to get printable type names.
- Register in the current scope with the mangled name.
- Set `type_info` to the concrete proc type.
- Add to `pending_specializations` for codegen.

## Tests

### Passing Tests (Stage 4 complete)

#### `tests/test_polymorphic_basics.odin`
```odin
package test

import "core:os"

identity :: proc($T: typeid, x: T) -> T {
    return x
}

double :: proc($T: typeid, x: T) -> T {
    return x + x
}

main :: proc() {
    a := identity(42)
    b := double(21)
    if a == 42 && b == 42 {
        os.exit(0)
    }
    os.exit(1)
}
```

#### `tests/test_polymorphic_unused.odin`
```odin
package test_polymorphic_unused

main :: proc() {
    os.exit(0)
}
// poly proc never called — compiles cleanly
unused :: proc(x: $T) -> T { return x }
```

#### `tests/test_polymorphic_specialization_dedup.odin`
```odin
package test

import "core:os"

double :: proc($T: typeid, x: T) -> T {
    return x + x
}

main :: proc() {
    a := double(21)
    b := double(42)
    c := double(100)
    // All three are int specializations — should share a single cached function
    if a == 42 && b == 84 && c == 200 {
        os.exit(0)
    }
    os.exit(1)
}
```

### Planned Test Suite (Future Stages)

#### Compile-time polymorphic value tests (`$N`)

##### `tests/test_polymorphic_array_len.odin`
```odin
package test_polymorphic_array_len
import "core:fmt"

sum_array :: proc($N: int, arr: [$N]int) -> int {
    s := 0
    for v in arr { s += v }
    return s
}

main :: proc() {
    a := [5]int{1, 2, 3, 4, 5}
    fmt.println(sum_array(5, a))    // 15

    b := [3]int{10, 20, 30}
    fmt.println(sum_array(3, b))    // 60
}
```

#### Compile-time polymorphic type tests (`$T`)

##### `tests/test_polymorphic_identity.odin`
```odin
package test_polymorphic_identity
import "core:fmt"

identity :: proc(x: $T) -> T {
    return x
}

main :: proc() {
    fmt.println(identity(42))           // int specialization → 42
    fmt.println(identity(3.14))          // f64 specialization → 3.14
}
```

##### `tests/test_polymorphic_double.odin`
```odin
package test_polymorphic_double
import "core:fmt"

double :: proc(x: $T) -> T {
    return x + x
}

main :: proc() {
    fmt.println(double(21))    // 42
    fmt.println(double(2.5))   // 5.0
}
```

##### `tests/test_polymorphic_swap.odin`
```odin
package test_polymorphic_swap
import "core:fmt"

swap :: proc($T: $T, $U: $U, x: T, y: U) -> (U, T) {
    return y, x
}

main :: proc() {
    a, b := swap(1, "hello")
    fmt.println(a, b)   // hello 1
}
```

#### Runtime polymorphic tests (mixed)

##### `tests/test_polymorphic_max.odin`
```odin
package test_polymorphic_max
import "core:fmt"

max_poly :: proc(a, b: $T) -> T {
    if a > b { return a }
    return b
}

main :: proc() {
    fmt.println(max_poly(3, 7))        // 7
    fmt.println(max_poly(2.5, 1.5))    // 2.5
}
```

#### Shorthand form tests (no explicit tag)

##### `tests/test_polymorphic_shorthand.odin`
```odin
package test_polymorphic_shorthand
import "core:fmt"

double2 :: proc(x: $T) -> T {
    return x + x
}

main :: proc() {
    fmt.println(double2(21))     // 42
    fmt.println(double2(2.5))     // 5.0
}
```

#### Polymorphic procs in overload bundles

##### `tests/test_polymorphic_overload_bundle.odin`
```odin
package test_polymorphic_overload_bundle
import "core:fmt"

print_int :: proc(x: int) -> int { return x }
print_poly :: proc(x: $T) -> T { return x }

show :: proc{print_int, print_poly}

main :: proc() {
    fmt.println(show(42))           // print_int wins (exact match) → 42
    fmt.println(show("hello"))      // print_poly specializes with $T=string → hello
    fmt.println(show(2.5))          // print_poly specializes with $T=f64 → 2.5
}
```

#### Cache & deduplication tests

##### `tests/test_polymorphic_specialization_dedup.odin`
```odin
package test_polymorphic_specialization_dedup
import "core:fmt"

double :: proc(x: $T) -> T { return x + x }

main :: proc() {
    a := double(21)
    b := double(42)
    c := double(100)
    fmt.println(a, b, c)   // 42 84 200 — all int, single specialization
}
```

#### Negative tests (expected_to_fail)

##### `tests/expected_to_fail/test_polymorphic_type_mismatch.odin`
```odin
package test_polymorphic_type_mismatch
import "core:fmt"

mismatch :: proc(a, b: $T) -> T {
    return a + b
}

main :: proc() {
    mismatch(1, "hello")   // $T is int from first arg, string from second → error
}
```
**Expected failure**: `cannot unify $T int with string`.

### Deferred (NOT to be added yet — would fail until `where` is implemented)
- `expected_to_fail/test_polymorphic_where_clause.odin`
  (`proc($T: $T) where type_of($T) == int` rejecting non-int call)

## Discrete Implementation Stages

### ✅ Stage 1: Grammar + detection only (no behavior change) — DONE

**Goal**: Add shorthand grammar rule, detection function `poly_signature_is_polymorphic`,
and hook into `sem_register_top_level_declaration` to mark symbols
`is_polymorphic = true` (but don't otherwise change behaviour — poly procs
still try to be analyzed normally, which will cause compile errors on real
poly code in the body).

**Files touched**:
- `src/symbols.h` — add `is_polymorphic`.
- `src/polymorphism.h` / `src/polymorphism.c` — new files with
  `poly_signature_is_polymorphic` only (stub the rest).
- `src/CMakeLists.txt` — add `polymorphism.c`.
- `src/semantic_analyser.c` — set `is_polymorphic` flag after register.

**Tests added**: `test_polymorphic_unused.odin` (passes — unused poly compiles).

**Verification**: `cmake --build build && ./tests/run_tests.sh`. All existing
tests plus the new one must pass.

### ✅ Stage 2: Skip body analysis for poly procs (silently) — DONE

**Goal**: `sem_analyse_procedure_literal` early-returns for poly procs if
not currently instantiating. Poly procs that are *called* will fail because
no specialization mechanism exists yet — but used poly procs are not in any
existing tests.

**Files touched**:
- `src/semantic_analyser.c` — early-return in `sem_analyse_procedure_literal`
  if `poly_signature_is_polymorphic && !ctx->currently_instantiating`.
- `src/sem_context.h` — add `bool currently_instantiating` flag (init false).
- `src/ir_gen_postfix.c` — emit clean error for direct poly calls.

**Tests**: No new tests. Existing tests still pass.

**Verification**: `cmake --build build && ./tests/run_tests.sh`.

### ✅ Stage 3: Env-stack instantiation (no caching, no codegen) — DONE

**Goal**: Implement `poly_resolve_call` using poly env stack. The semantic
analyser builds a `PolyEnv` from call-site args, pushes it onto the
`SemContext` poly env stack, re-runs `sem_analyse_procedure_literal` on the
original proc AST (which now resolves correctly through the env), and registers
a specialization symbol. **No codegen yet** — the specialization is registered
but no LLVM IR is emitted. Verification only checks that semantic analysis
succeeds without errors.

**Files touched**:
- `src/polymorphism.h` — full public API (PolyEnv, poly_resolve_call, etc.)
- `src/polymorphism.c` — env stack, origin tracking, poly_build_env_from_args,
  poly_resolve_call implementation
- `src/semantic_analyser.h` — poly_env_stack[], poly_env_stack_depth,
  pending_specializations fields
- `src/sem_context.c` — init/destroy new fields
- `src/sem_type_resolver.c` — POLY_IDENT dispatch, poly-aware identifier resolver
- `src/sem_evaluate_expr.c` — poly branch in POSTFIX_CALL handlers
- `src/semantic_analyser.c` — register origin, handle POLY_IDENT in sig resolution

**Tests**: No runtime tests (no codegen). Manual verification that
`test_polymorphic_identity.odin` etc. don't produce semantic errors.
All existing tests still pass.

### ✅ Stage 4: Codegen for specializations + UAF fix — DONE

**Goal**: Drain `pending_specializations` in `ir_generate()` after each file's
main top-level pass; each spec's specialization is processed via the standard
top-level path. Fix pre-existing UAF bugs in `scope_lists.c` and scope-free
ordering before this stage. Mnemonic name mangling for specialization symbols.

**Bug fixes applied during this stage**:
- `scope_lists.c` — hash key `strdup` to prevent dangling pointer UAF
- `generator_pop_scope` / `generator_free_deferred_scopes` — defer `scope_free`
  until after codegen
- `ir_gen_postfix_call` — 3-priority dispatch (resolved_symbol → sym → cur_type)

**Files touched**:
- `src/llvm_ir_generator.c` — `ir_gen_top_level_decl` early-return if
  `sym->is_polymorphic`; `ir_generate()` drain + codegen pending specs
- `src/polymorphism.c` — `poly_make_mangled_name` helper
- `src/scope_lists.c` — fix `free(existing->name)` UAF
- `src/scope.c` / `src/semantic_analyser.c` — fix scope-free ordering
  (defer scope_free until after codegen, or use arena for symbol_t)
- `src/ir_gen_postfix.c` — fix dispatch priority for resolved_symbol

**Tests added**: `test_polymorphic_basics.odin` compiles, links, runs correctly.

**Verification**: All tests pass. `test_polymorphic_basics.odin` calls
`identity(42)` and expects 42 back.

### ✅ Stage 5: Specialization cache + dedup — DONE

**Goal**: Proper cache that maps mangled name → `PolySpecialization*`.
Before analyzing the proc body on each call, check the cache. On hit, return
the existing specialization immediately — no redundant re-analysis.

**Implementation** (`src/polymorphism.c`):
- Added static `PolyCacheEntry` array with `poly_cache_lookup()` (linear scan)
  and `poly_cache_store()` functions.
- Modified `poly_resolve_call`: cache check placed **after** mangled name
  generation but **before** `poly_env_push` / `sem_analyse_procedure_literal`.
  Cache is populated right after a new specialization is created.
- Removed the old post-analysis global-scope dedup check (lines 559-574) —
  the pre-analysis cache replaces it.

**Files touched**:
- `src/polymorphism.c` — cache data structures, lookup/store helpers,
  modified `poly_resolve_call` to check cache before analysis.

**Tests added**: `test_polymorphic_specialization_dedup.odin` — calls
`double(21)`, `double(42)`, `double(100)` (all `int` specializations);
verifies all three return correct values.

**Verification**: 159/159 tests pass.

### ✅ Stage 6: Polymorphic procs in overload bundles — DONE

**Goal**: `sem_resolve_overload_bundle_call` integrates poly candidates.

**Files touched**:
- `src/sem_evaluate_expr.c` — in the per-candidate loop, add the
  "candidate is polymorphic → score + instantiate" branch.

**Tests added**: `test_polymorphic_overload_bundle.odin`.

**Negative test added**:
`tests/expected_to_fail/test_polymorphic_type_mismatch.odin`.

**Bugs fixed during this stage**:
1. `sem_resolve_poly_ident_type` — stripped `$` prefix before `poly_env_lookup_type`
   (env stores `"T"` but raw text was `"$T"`).
2. `poly_build_env_from_args` — `x: $T` was treated as a type-param declaration
   (skipping `param_idx` and binding); fixed by distinguishing poly ident in name
   position vs. type position.
3. `ir_gen_register_params` — `AST_NODE_POLY_IDENT` not recognized as a type node
   (same bug as `semantic_analyser.c:678`, which was fixed in an earlier session).

**Verification**: All tests pass; expected-to-fail test fails as expected.

### ✅ Stage 7: `$N` integer polymorphic parameters — DONE

**Goal**: Support `[$N]int` array sizes in polymorphic proc signatures, where
`$N` is bound from the integer argument at the call site. For example:
`sum_array :: proc($N: int, arr: [$N]int) -> int`.

**Implementation** (`src/polymorphism.c`, `src/llvm_ir_generator.c`, etc.):
- `poly_build_env_from_args`: `$N: int` parameters create `POLY_SLOT_INT` entries
  that map the poly ident name to the argument's integer value.
- `poly_unify_poly_idents_in_type`: walks type AST nodes for `POLY_IDENT` in
  array-size position and creates `POLY_SLOT_INT` entries.
- `poly_env_push` / `poly_env_pop`: env stack for int values (in addition to types).
- `poly_env_lookup_int`: retrieves bound int value by name.
- `ir_gen_pending_specialization`: registers `$N` constants in the specialization
  scope as `has_const_int_val` entries before body codegen.

**Bugs fixed during this stage**:
1. **`poly_env_pop` use-after-free**: `poly_env_push` does a shallow struct copy;
   the pushed copy and local `env` share the same `strdup`'d `name` pointers.
   After `poly_env_pop` frees those pointers, reading them via the local `env`
   yields stale data. **Fix**: extract poly int values from the local `env`
   **before** `poly_env_pop` is called, using a local snapshot array.
   Saves them into `PolySpecialization.poly_int_names` / `poly_int_values` /
   `poly_int_count`.
2. **Stale AST `resolved_symbol`**: `ir_gen_postfix_expression` used
   `pe_child->resolved_type` which could be stale from a previous instantiation
   (same AST shared across all specializations). **Fix**: scope-symbol type is
   now preferred over `pe_child->resolved_type`.
3. **Aggregate-by-value arg loading**: In `ir_gen_postfix_call` Phase 5, when
   the argument is a pointer (from alloca for composite types) but the param
   expects an array or struct by value, a `LLVMBuildLoad2` is emitted to pass
   the value.

**Files touched**:
- `src/polymorphism.h` — `PolySpecialization.poly_int_names`, `poly_int_values`,
  `poly_int_count` fields.
- `src/polymorphism.c` — poly int snapshot before `poly_env_pop`;
  `poly_unify_poly_idents_in_type` creates `POLY_SLOT_INT` entries.
- `src/llvm_ir_generator.c` — `ir_gen_pending_specialization` registers $N
  constants; `ir_gen_identifier` checks `has_const_int_val`.
- `src/ir_gen_postfix.c` — prefers scope-symbol type; loads aggregate values
  from pointers.
- `src/ir_gen_statement.c` — small fix for compound statement handling.
- `src/odin_grammar.gdl` — `$N: int` parameter grammar.
- `src/sem_type_resolver.c` — poly ident resolution for array sizes.
- `src/semantic_analyser.c` — handle `$N` poly idents in parameter registration.
- `src/polymorphism.c` — poly int env entries in `poly_build_env_from_args` and
  `poly_unify_poly_idents_in_type`.

**Tests added**: `test_polymorphic_array_size.odin` — calls `sum_array(5, a)`
where `a: [5]int`, verifies the result `15`.

**Verification**: All 161 tests pass. Intermediate debugging stubs
(`test_poly_array*.odin`, `test_poly_dollar_test.odin`) deleted.

### ✅ Stage 8: Nested polymorphism + dynamic env stack — DONE

**Goal**: Support poly procs calling other poly procs (nested polymorphism).
Convert the poly env stack from fixed-size to a dynamic array.

**Implementation** (`src/polymorphism.c`, `src/polymorphism.h`, `src/semantic_analyser.h`,
`src/sem_context.c`):

1. **Dynamic poly env stack**:
   - `SemContext.poly_env_stack` changed from `PolyEnv[MAX_POLY_STACK_DEPTH]`
     to `PolyEnv *` with `poly_env_stack_capacity`.
   - `poly_env_push` uses `realloc` to grow the stack when full (starts at 4,
     doubles on each growth).
   - `poly_env_pop` and all lookup functions (`poly_env_lookup_type`,
     `poly_env_lookup_int`) work unchanged with pointer arithmetic.
   - `sem_context_destroy` frees remaining env entry names and the array.
   - Removed `#define MAX_POLY_STACK_DEPTH` from `polymorphism.h`.

2. **`currently_instantiating` flag save/restore**:
   - `poly_resolve_call` now saves `ctx->currently_instantiating` before setting
     it to `true`, and restores the previous value after `poly_env_pop`. This
     prevents an inner `poly_resolve_call` from clearing the outer
     instantiation flag.

**Bugs fixed during this stage**:
- **`currently_instantiating` flag clobber**: When `poly_resolve_call` was called
  from within an already-active instantiation (nested poly call), it unconditonally
  set `ctx->currently_instantiating = false` after returning. This caused
  `sem_analyse_procedure_literal` for the outer proc to early-return, skipping
  body analysis of the outer specialization.

**Files touched**:
- `src/polymorphism.h` — removed `MAX_POLY_STACK_DEPTH`; updated comment.
- `src/polymorphism.c` — `poly_env_push` uses realloc; `poly_resolve_call`
  saves/restores `currently_instantiating`.
- `src/semantic_analyser.h` — `poly_env_stack` changed to `PolyEnv *`; added
  `poly_env_stack_capacity`.
- `src/sem_context.c` — init/destroy dynamic array.

**Tests added**:
- `test_polymorphic_nested.odin` — `double` calls `identity` (both poly, same `$T`).
- `test_polymorphic_nested_deep.odin` — `triple` → `double` → `identity` (3 levels).
- `test_polymorphic_nested_diff_params.odin` — different `$T`/`$U` param names.
- `test_polymorphic_nested_array.odin` — `wrap_sum` calls `sum_array` (both with `$N`).

**Verification**: All 165 tests pass (161 previous + 4 new).

### ✅ Stage 9: Where-clause evaluation — DONE

**Goal**: Evaluate `where` clause expressions on polymorphic procedure
signatures, enabling compile-time type constraints.

**Implementation** (`src/polymorphism.c`):
- `poly_resolve_type_for_where`: resolves type nodes via poly env stack or
  type registry (unwraps `AST_NODE_TYPE_NAME`).
- `poly_eval_typeid_of`: evaluates `typeid_of(T)` by resolving T via poly env.
- `poly_eval_size_of`: evaluates `size_of(T)` via poly env + LLVM type size.
- `poly_eval_where_expr`: recursive evaluator for `==`, `!=`, `&&`, `||`, `!`,
  arithmetic, `typeid_of`, `size_of`.
- `poly_find_where_clause` / `poly_evaluate_where_clause`: find and evaluate
  the where clause on a ProcedureDefinition's signature.
- Where clause evaluated in `poly_resolve_call` AFTER env push so poly idents
  are bound. Returns NULL on constraint violation (caller decides error vs skip).

**Bugs fixed**:
1. Implicit function declaration in `polymorphism.c` — `sem_resolve_type_expr()`
   called without including `sem_type_resolver.h`, causing pointer truncation.
2. ARGUMENT_LIST comma-chain — `poly_build_env_from_args` and arg evaluation
   loop in `sem_evaluate_expr.c` decomposed via `sem_collect_comma_chain_args()`.

**Tests**:
- `test_where_clause.odin` — typeid_of int/f64, size_of matching.
- `expected_to_fail/test_where_clause_fail.odin` — size_of mismatch.

**Verification**: All 167 tests pass.

### ✅ Stage 10: Where-clause overload filtering — DONE

**Goal**: Use where clauses to filter overload bundle candidates. Poly-only
overload bundles with complementary where clauses correctly dispatch to the
matching candidate.

**Implementation**: Already handled by existing infrastructure — `poly_resolve_call`
evaluates where clauses, and the overload bundle loop in `sem_resolve_overload_bundle_call`
skips candidates returning NULL from `poly_resolve_call`.

**Tests**:
- `test_poly_overload_where.odin` — poly-only bundle (`dispatch :: proc{identity_int, identity_f64}`
  with `where typeid_of(T)==typeid_of(int)` and `typeid_of(T)==typeid_of(f64)`),
  size_of-based dispatch, logical OR where clause.
- `expected_to_fail/test_poly_overload_where_ambiguous.odin` — two candidates
  with identical where clauses → ambiguity error.

**Verification**: All 169 tests pass (167 previous + 2 new).

### 🔲 Remaining deferred features

Documented separately — these milestones will be built on top of the
stable Stage 1–10 work.

## Estimated Scope

- New code: ~800–1000 lines (env-stack approach is smaller than clone)
- Existing-file changes: ~150 lines total across 8 files
- Tests: ~10+ new test files in stages 3–6
- Estimated complexity: medium-high but flattened by staging
- **Current progress**: Stages 1–10 complete (including UAF bug fixes,
  postfix call dispatch fix, specialization cache, overload bundle poly
  support, `$N` integer polymorphic parameters, nested polymorphism,
  dynamic poly env stack, where clause evaluation, and where-clause
  overload filtering). Explicit `$T: typeid` / `$N: int` parameter
  syntax verified. **Polymorphic forward declarations verified** (no code
  changes needed — `poly_register_origin` overwrites on re-registration).
  **171/171 tests passing**.

## Risks / Open Concerns

1. **Env-stack correctness** — poly env must be active during the entire
   procedure body analysis for all identifier lookups to resolve correctly.
   The env is pushed before `sem_analyse_procedure_literal` and popped after.
2. **Recursion inside instantiated procs** — specialization's symbol must be
   registered in parent scope *before* analyzing its body, mirroring the
   non-poly recursion fix.
3. **Poly-proc called from inside another poly-proc** — ✅ DONE (Stage 8).
   Initial tests avoid this pattern.
4. **Forward declarations of poly procs** — ✅ VERIFIED (no code changes
   needed; `poly_register_origin` overwrites origin on re-registration).
5. **Cross-package polymorphic procs** — deferred (Stage 7).
6. **Polymorphic return-type inference via `auto_cast` / untyped literals**
   — out of scope. Tests always bind `$T`/`$U` via a parameter.
7. **Bare `T` vs `$T` in return types** — the env-stack approach relies on
   `sem_resolve_type_identifier` also checking the poly env. This means a
   bare `T` in the return type resolves to the poly bound type when the
   env is active. This is correct for the shorthand form `proc(x: $T) -> T`.
