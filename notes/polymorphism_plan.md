# Implementation Plan: `$T` / `$N` Compile-Time Polymorphic Parameters

A monomorphization-based implementation of polymorphic procedures for the Odin
compiler. Per call-site, type variables in the procedure signature are bound
to concrete types and integer constants from the argument expressions; the proc
AST is cloned with substitutions applied; the resulting specialization is
analyzed & codegen'd as a normal procedure. A cache keyed on
`(proc symbol, arg type pointer tuple)` ensures only one specialization per
unique binding.

## Confirmed Scope

User decisions taken at plan time:

- **Both syntactic forms** — `proc($T: $T)` (explicit) and `proc(x: $T)` (shorthand)
- **Specialization cache key** = type-pointer tuple
- **`where` clauses** — skipped initially (parse + ignore for now)
- **Overload bundles** — polymorphic candidates supported in bundles from start
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

2. **Nested polymorphism** — a polymorphic proc calling another polymorphic
   proc that hasn't yet been instantiated for the outer env. Tests must avoid
   this pattern; e.g. a polymorphic `sort` calling a polymorphic `swap` where
   the latter is a free top-level symbol. **Plan**: Pass the outer env down
   into the inner instantiation when `poly_instantiate` is called during the
   outer body's analysis.

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

## Architecture Overview

Pipeline mirrors the existing semantic / IR pipeline:

1. **Detection (semantic pass 1)** — when registering a `ConstantDecl`
   whose value is a `ProcedureLiteral`, scan its signature for
   `AST_NODE_POLY_IDENT`. If found, mark the symbol as polymorphic and
   **skip body analysis**; register the proc symbol with `type_info = NULL`.

2. **Instantiation (semantic, at call-site)** — when
   `sem_evaluate_postfix_call` encounters a callee symbol marked polymorphic:
   - Evaluate argument types
   - Unify arg types with poly params to build an instantiation environment
   - Look up the cache by `(poly_origin, arg_type_tuple_hash)`
   - On miss: clone the proc AST, substitute poly idents with concrete types /
     values, run `sem_analyse_procedure_literal` on the clone, register the
     specialization as a top-level symbol, store in cache.

3. **Codegen** — specialization symbols carry a pointer to their cloned
   specialization AST. After each file's main semantic pass, the driver
   drains the `pending_specializations` queue and codegen's each as if it
   were a regular top-level procedure. Mangling:
   `<proc_name>__poly_<argtypecanon1>_<argtypecanon2>...`.

Polymorphism-specific code lives in two new modules plus one shared module,
all kept separate from the existing code. Existing files are touched only
with small additions (hook points); no refactoring of unrelated functionality.

## New Modules

### 1. `src/polymorphism.h` / `src/polymorphism.c`

Public API:

```c
// Read-only detection: scan a procedure signature AST for any $T/$N usage.
bool poly_signature_is_polymorphic(odin_grammar_node_t * sig_node);

// Instantiate (or look up in cache) a specialization of the poly proc whose
// origin symbol is `poly_symbol`, for the given concrete argument types.
// On cache hit, returns the existing specialization without re-analyzing.
// On miss, clones the AST, substitutes, analyses, registers in scope & cache.
PolySpecialization * poly_resolve_call(
    SemContext * ctx,
    symbol_t * poly_symbol,
    TypeDescriptor const ** arg_types,
    int arg_count
);

// Score a candidate proc type against concrete arg types for overload-bundle
// resolution. Returns -1 for no match, 0 for exact, higher = more conversions.
// If candidate type uses $T/$N polymorphism, the unification env is written
// to `out_env` / `out_env_count` for downstream instantiation.
int poly_score_candidate(
    SemContext * ctx,
    TypeDescriptor const * candidate_proc_type,
    TypeDescriptor const ** arg_types,
    int arg_count,
    PolyEnv * out_env,
    int * out_env_count
);
```

### 2. `src/polymorphism_shared.h` / `src/polymorphism_shared.c`

Shared matching/unification used by both polymorphic call resolution and
existing overload-bundle resolution. Factored out of
`sem_resolve_overload_bundle_call`:

- `poly_match_arg_to_param(SemContext * ctx, TypeDescriptor * arg_type, TypeDescriptor * param_type, PolyEnv * env, int * env_count)`
- `poly_score_candidate(...)` (declared above)

## Modifications to Existing Files

### `src/symbols.h`
Add a single field to `symbol_t`:
```c
bool is_polymorphic;
```

**NOTE**: The plan originally proposed adding `poly_origin_ast` and `poly_cache` pointers directly to `symbol_t`, but doing so shifts the memory layout of `symbol_t` in a way that exposes pre-existing use-after-free bugs in `scope_lists.c` (`free(existing->name)` invalidates the hash-table entry's key pointer) and the scope-cleanup ordering (`scope_free` runs while AST nodes still hold `node->resolved_symbol` pointers into freed symbol_t blocks). These UAFs are pre-existing latent bugs that manifest only after specific memory-layout shifts. They are deferred to a future bug-fix stage (not part of polymorphism work); for now, polymorphism-specific metadata (origin AST, cache) lives in side tables keyed by `symbol_t*` and managed inside `polymorphism.c`.

### `src/sem_context.h`
Add:
```c
odin_grammar_node_t ** pending_specializations;
int pending_spec_count, pending_spec_capacity;
```

### `src/odin_grammar.gdl`
Add shorthand `Identifier Colon PolyIdent` alternative to `Parameter`:
```
Parameter = KwUsing?
          ( (PolyIdent Colon)? Identifier Colon VariadicMarker? TypePrefix
          | Identifier Colon PolyIdent )
          @AST_ACTION_PARAMETER;
```

### `src/semantic_analyser.c`
- `sem_register_top_level_declaration` (~line 760): after `sem_resolve_procedure_signature`,
  call `poly_signature_is_polymorphic(sig_node)`. If true:
  - Set `sym->is_polymorphic = true`, `sym->poly_origin_ast = value_node`.
  - Set `sym->value.type_info = NULL` (don't pre-bind proc type).
  - Register the proc under its declared name (still findable by callers).
- `sem_analyse_procedure_literal` (~line 577): at entry, if
  `poly_signature_is_polymorphic(sig_node) && !ctx->currently_instantiating`
  → return early (poly procs are not analyzed standalone; only their clones).
- `sem_resolve_procedure_signature` (~line 492): when parameter type node is
  `AST_NODE_POLY_IDENT`, record it but do NOT call `sem_resolve_type_expr`
  (which would return NULL). For poly sigs return a sentinel poly-proc
  TypeDescriptor with null param_types.

### `src/sem_evaluate_expr.c`
At the two `AST_NODE_POSTFIX_CALL` handlers (lines 1197 and 1306 — package-
qualified and unqualified call paths), add a branch BEFORE the existing
`type->kind == TD_KIND_PROC` test:

```c
if (sym && sym->is_polymorphic) {
    // evaluate args, build type list
    TypeDescriptor const * arg_types[MAX_ARGS];
    int arg_count = 0;
    collect_arg_types(ctx, arg_list, arg_types, &arg_count);
    PolySpecialization * spec = poly_resolve_call(ctx, sym, arg_types, arg_count);
    if (spec && spec->symbol) {
        op->resolved_symbol = spec->symbol;
        op->resolved_type = (TypeDescriptor *)spec->symbol->value.type_info;
    }
    break;
}
```

In `sem_resolve_overload_bundle_call` (line 1810-1861) per-candidate loop,
add a branch BEFORE the assignability check:

```c
if (candidate_symbols[ci]->is_polymorphic) {
    PolyEnv env[MAX_POLY];
    int env_count = 0;
    int score = poly_score_candidate(ctx, candidate_types[ci],
                                     arg_types, arg_count, env, &env_count);
    if (score >= 0) {
        PolySpecialization * spec = poly_resolve_call(
            ctx, candidate_symbols[ci], arg_types, arg_count);
        if (spec && spec->symbol) {
            best_match = spec->symbol;
            match_count++;
        }
    }
    continue;
}
```

### `src/llvm_ir_generator.c`
- `ir_gen_top_level_decl` (~line 990): at entry, check `sym->is_polymorphic`.
  If true → return early (poly procs emit no LLVM function from their origin
  decl; only their instantiations do).
- Extend `ir_generate()` end-loop: after iterating the file's top-level
  decls, drain `ctx->pending_specializations` and codegen each cloned
  `AST_NODE_CONSTANT_DECL` node via the standard top-level path.
- Mangling: `attrs->link_name = "<proc>__poly_<mangle>"` set on the cloned
  AST's metadata.

### `src/ir_gen_postfix.c`
In `ir_gen_postfix_call` (line 170-203), if the callee lookup resolves to a
poly-marked symbol, return a "no direct call to polymorphic origin" error
(invalid use). Direct calls must always go through `op->resolved_symbol`
which is the specialization symbol set by the semantic analyser.

### `src/CMakeLists.txt`
Add `polymorphism.c` and `polymorphism_shared.c` to the odinc target.

## Semantic Details

### Polymorphic Param Detection (`poly_signature_is_polymorphic`)
1. Walk `sig_node->children` for `PARAMETER_LIST` → `PARAMETERS` → `PARAMETER[]`.
2. For each `PARAMETER`:
   - Scan children for `AST_NODE_POLY_IDENT` (explicit tag OR shorthand).
   - If the tag's name appears as `AST_NODE_POLY_IDENT` in `TypePrefix`
     → binds `SLOT_TYPE`.
   - Else if `TypePrefix` resolves to a normal type AND the poly name is
     used elsewhere in the signature (e.g. `[$N]int`) → binds
     `SLOT_INT_CONST`.
3. Walk `Returns` for the same poly uses.
4. If any `PolyIdent` is found in params OR returns → return true.

### Instantiation Environment
- Initialize empty `PolyEnv[16]` (limit 16 poly vars per proc).
- For each poly-typed param at position `i` (its declared type is a `PolyIdent`):
  - Look up `arg_types[i]` from call site.
  - Search `env` for the poly name: if already bound, verify equality with
    `arg_types[i]` (else: type mismatch error). If not yet bound, add it.
  - Set the cloned param's type to the concrete type.
- For each `SLOT_INT_CONST` poly (e.g. `$N: int`):
  - If call-site supplies a constant int arg → bind from that.
  - Else if a sibling poly-typed param uses it (e.g. `[$N]int`), derive
    `$N = len(arg_type[i])` from the array argument.
  - Else → emit "cannot infer $N" error.

### AST Clone + Substitute
1. Deep clone the `ProcedureDefinition` (and the wrapping `ConstantDecl`).
2. Reuse `text` pointers (lexer-owned, safe to share).
3. Reset `resolved_type` and `resolved_symbol` to NULL in every cloned node.
4. Walk the clone recursively. For each `AST_NODE_POLY_IDENT` node:
   - Look up name in `env`.
   - For `SLOT_TYPE` binding → synthesize an `AST_NODE_BASIC_TYPE` /
     `AST_NODE_IDENTIFIER` node that resolves to the bound `TypeDescriptor`.
     Pre-populate the cloned node's `resolved_type` so
     `sem_resolve_type_expr` picks it up.
   - For `SLOT_INT_CONST` binding → synthesize an `AST_NODE_INTEGER_VALUE`
     with `text = "<int>"` and a pre-resolved value.
5. Run `sem_resolve_procedure_signature` on the cloned sig → produces a
   concrete `TypeDescriptor*` for the specialization.
6. Register specialization's name in the parent scope **before** analyzing
   the body (enables recursion-as-self-call).
7. Run `sem_analyse_procedure_literal` on the cloned body with a fresh scope
   that pre-injects all poly var names as `SYMBOL_TYPE` / `SYMBOL_CONSTANT`
   entries.

### Cache
- Hash table in `polymorphism.c` keyed on
  `(uint64_t poly_origin_ptr, uint64_t arg_type_tuple_hash)`.
- `arg_type_tuple_hash` = combined hash of `arg_types[i]` pointers
  (XOR + rotate).
- Linear scan on hash collision (chained buckets).
- Lifetime: lives for the entire compilation. Specializations never evicted.

### Mangled Names
- Format: `<origin_name>__poly_<argtypecanon1>_<argtypecanon2>...`
- Example: `double__poly_int` for `double($T: $T)` called with `int`.
- Use `type_write_canonical_name` to get printable type names.

## Tests

### Compile-time polymorphic value tests (`$N`)

#### `tests/test_polymorphic_array_len.odin`
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

### Compile-time polymorphic type tests (`$T`)

#### `tests/test_polymorphic_identity.odin`
```odin
package test_polymorphic_identity
import "core:fmt"

identity :: proc($T: $T, x: T) -> T {
    return x
}

main :: proc() {
    fmt.println(identity(42))           // int specialization → 42
    fmt.println(identity(3.14))          // f64 specialization → 3.14
}
```

#### `tests/test_polymorphic_double.odin`
```odin
package test_polymorphic_double
import "core:fmt"

double :: proc($T: $T, x: T) -> T {
    return x + x
}

main :: proc() {
    fmt.println(double(21))    // 42
    fmt.println(double(2.5))   // 5.0
}
```

#### `tests/test_polymorphic_swap.odin`
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

### Runtime polymorphic tests (mixed)

#### `tests/test_polymorphic_max.odin`
```odin
package test_polymorphic_max
import "core:fmt"

max_poly :: proc($T: $T, a, b: T) -> T {
    if a > b { return a }
    return b
}

main :: proc() {
    fmt.println(max_poly(3, 7))        // 7
    fmt.println(max_poly(2.5, 1.5))    // 2.5
}
```

### Shorthand form tests (no explicit tag)

#### `tests/test_polymorphic_shorthand.odin`
```odin
package test_polymorphic_shorthand
import "core:fmt"

double2 :: proc(x: $T) -> $T {
    return x + x
}

main :: proc() {
    fmt.println(double2(21))     // 42
    fmt.println(double2(2.5))     // 5.0
}
```

### Polymorphic procs in overload bundles

#### `tests/test_polymorphic_overload_bundle.odin`
```odin
package test_polymorphic_overload_bundle
import "core:fmt"

print_int :: proc(x: int) -> int { return x }
print_poly :: proc($T: $T, x: T) -> T { return x }

show :: proc{print_int, print_poly}

main :: proc() {
    fmt.println(show(42))           // print_int wins (exact match) → 42
    fmt.println(show("hello"))      // print_poly specializes with $T=string → hello
    fmt.println(show(2.5))          // print_poly specializes with $T=f64 → 2.5
}
```

### Cache & deduplication tests

#### `tests/test_polymorphic_specialization_dedup.odin`
```odin
package test_polymorphic_specialization_dedup
import "core:fmt"

double :: proc($T: $T, x: T) -> T { return x + x }

main :: proc() {
    a := double(21)
    b := double(42)
    c := double(100)
    fmt.println(a, b, c)   // 42 84 200 — all int, single specialization
}
```

### Negative tests (expected_to_fail)

#### `tests/expected_to_fail/test_polymorphic_type_mismatch.odin`
```odin
package test_polymorphic_type_mismatch
import "core:fmt"

mismatch :: proc($T: $T, a: T, b: T) -> T {
    return a + b
}

main :: proc() {
    mismatch(1, "hello")   // $T is int from first arg, string from second → error
}
```
**Expected failure**: `cannot unify $T int with string`.

#### `tests/test_polymorphic_unused.odin` (positive test!)
```odin
package test_polymorphic_unused
import "core:fmt"

unused :: proc($T: $T, x: T) -> T { return x }

main :: proc() {
    fmt.println("nothing")   // poly proc never called — compiles cleanly
}
```
Asserts: poly procs with no call sites compile cleanly (no body emitted).

### Deferred (NOT to be added yet — would fail until `where` is implemented)
- `expected_to_fail/test_polymorphic_where_clause.odin`
  (`proc($T: $T) where type_of($T) == int` rejecting non-int call)

## Discrete Implementation Stages

Each stage is independently committable and must keep all existing tests
passing. This minimizes debugging surface area when a regression appears.

### Stage 1: Grammar + detection only (no behavior change)

**Goal**: Add shorthand grammar rule, detection function `poly_signature_is_polymorphic`,
and hook into `sem_register_top_level_declaration` to mark symbols
`is_polymorphic = true` (but don't otherwise change behaviour — poly procs
still try to be analyzed normally, which will cause compile errors on real
poly code in the body).

**Files touched**:
- `src/odin_grammar.gdl` — shorthand `Parameter` rule.
- `src/symbols.h` — add `is_polymorphic`, `poly_origin_ast`, `poly_cache`.
- `src/polymorphism.h` / `src/polymorphism.c` — new files with
  `poly_signature_is_polymorphic` only (stub the rest).
- `src/CMakeLists.txt` — add `polymorphism.c`.
- `src/semantic_analyser.c` — set `is_polymorphic` flag after register.

**Tests added**: `test_polymorphic_unused.odin` (passes — unused poly compiles).

**Verification**: `cmake --build build && ./tests/run_tests.sh`. All 156
existing tests plus the new one must pass.

### Stage 2: Skip body analysis for poly procs (silently)

**Goal**: `sem_analyse_procedure_literal` early-returns for poly procs if
not currently instantiating. Poly procs that are *called* will fail because
no specialization mechanism exists yet — but used poly procs are not in any
existing tests.

**Files touched**:
- `src/semantic_analyser.c` — early-return in `sem_analyse_procedure_literal`
  if `poly_signature_is_polymorphic && !ctx->currently_instantiating`.
- `src/sem_context.h` — add `bool currently_instantiating` flag (init false).
- `src/sem_evaluate_expr.c` — when resolving callee symbol, if
  `is_polymorphic` flag is set, emit a useful error ("polymorphic
  procedure called but specialization not yet implemented"). Stage 3
  replaces this error with actual instantiation.

**Tests**: No new tests. Existing tests still pass. Calling a poly proc
would now give a clear error.

**Verification**: `cmake --build build && ./tests/run_tests.sh`.

### Stage 3: AST clone + substitute + instantiation (no caching)

**Goal**: Implement the core `poly_resolve_call` without caching. Each
call-site creates a fresh specialization (correct but inefficient — creates
duplicate specializations for repeated identical calls).

**Files touched**:
- `src/polymorphism.c` — implement:
  - `poly_collect_poly_params` — gather named poly vars from signature
  - `poly_collect_arg_types` — evaluate arg expression types at call-site
  - `poly_unify` — match args against params, build `PolyEnv`
  - `poly_clone_ast` — deep clone (reuse `text`, NULL `resolved_type`)
  - `poly_substitute_in_ast` — walk clone, replace `AST_NODE_POLY_IDENT`
  - `poly_instantiate` (no-cache variant) — drive clone + analyse +
    register in parent scope + add to `pending_specializations`
- `src/polymorphism.h` — full public API.
- `src/semantic_analyser.c` — `sem_resolve_procedure_signature`
  understands `AST_NODE_POLY_IDENT` in param type slots (records instead
  of resolving).
- `src/sem_evaluate_expr.c` — replace Stage-2 error with:
  - `poly_resolve_call(ctx, sym, arg_types, arg_count)` call
  - Set `op->resolved_symbol` to specialization's symbol.

**Tests added**: `test_polymorphic_identity.odin`,
`test_polymorphic_double.odin`, `test_polymorphic_shorthand.odin`,
`test_polymorphic_max.odin`, `test_polymorphic_unused.odin`.

**Verification**: `cmake --build build && ./tests/run_tests.sh`. New poly
tests pass; existing tests unaffected.

### Stage 4: Codegen for specializations

**Goal**: Drain `pending_specializations` in `ir_generate()` after each
file's main top-level pass; each spec's cloned `AST_NODE_CONSTANT_DECL` is
processed via the standard top-level path. Mangling the function name
prevents symbol collisions with the original poly symbol. The original
poly symbol emits nothing (already skipped in Stage 2 / Stage 3).

**Files touched**:
- `src/llvm_ir_generator.c`:
  - `ir_gen_top_level_decl` early-return if `sym->is_polymorphic`.
  - `ir_generate()` end: drain `ctx->pending_specializations` — for each,
    set its `attrs->link_name` to the mangled specialization name, then
    call `ir_gen_top_level_decl` on the cloned node.
- `src/polymorphism.c` — `poly_make_mangled_name` helper.
- `src/ir_gen_postfix.c` — guard: if direct callee symbol is poly (not
  via `resolved_symbol` specialization path), emit error.

**Tests added**: `test_polymorphic_array_len.odin` (uses `$N` value poly),
`test_polymorphic_swap.odin` (two poly vars), `test_polymorphic_unused.odin`.

**Verification**: All Stage-3 tests still pass; new tests work; no
regressions in the existing 156.

### Stage 5: Specialization cache + dedup

**Goal**: Hash-table cache on `(poly_origin, arg_type_tuple_hash)`. Before
instantiating, consult cache. On hit, return existing specialization.

**Files touched**:
- `src/polymorphism.c` — add `PolySpecializationCache`, modify
  `poly_resolve_call` to look up the cache before invoking
  `poly_instantiate`, and store the result in the cache after.

**Tests added**: `test_polymorphic_specialization_dedup.odin`.

**Verification**: Manual IR inspection (`--keep-temps`) confirms a single
`double__poly_int` function for three int calls. Tests pass.

### Stage 6: Polymorphic procs in overload bundles

**Goal**: `sem_resolve_overload_bundle_call` integrates poly candidates.

**Files touched**:
- `src/polymorphism_shared.h` / `src/polymorphism_shared.c` — refactor
  matching logic from `sem_resolve_overload_bundle_call`; expose
  `poly_score_candidate`.
- `src/sem_evaluate_expr.c` — in the per-candidate loop, add the
  "candidate is polymorphic → score + instantiate" branch.
- `src/CMakeLists.txt` — add `polymorphism_shared.c`.

**Tests added**: `test_polymorphic_overload_bundle.odin`.

**Negative test added**:
`tests/expected_to_fail/test_polymorphic_type_mismatch.odin`.

**Verification**: All tests pass; expected-to-fail test fails as expected.

### Stage 7 (deferred): `where` clauses, nested polymorphism,
cross-package polymorphic procs, polymorphic structs.

Documented separately — these milestones will be built on top of the
stable Stage 1–6 work.

## Estimated Scope

- New code: ~1200–1500 lines
- Existing-file changes: ~150 lines total across 6 files
- Tests: ~10+ new test files in stages 3–6
- Estimated complexity: high but flattened by staging

## Risks / Open Concerns

1. **AST deep-clone correctness** — must clone `list.children` arrays,
   `metadata` (clone or recreate — don't share), reset
   `resolved_type`/`resolved_symbol`, share `text`.
2. **Recursion inside instantiated procs** — specialization's symbol must
   be registered in parent scope *before* analyzing its body, mirroring
   the non-poly recursion fix (see AGENTS.md).
3. **Poly-proc called from inside another poly-proc** — deferred
   (Stage 7). Initial tests avoid this pattern.
4. **Forward declarations of poly procs** — deferred (Stage 7).
5. **Cross-package polymorphic procs** — deferred (Stage 7).
6. **Polymorphic return-type inference via `auto_cast` / untyped literals**
   — out of scope. Tests always bind `$T`/`$U` via a parameter.

None of these are blockers for the staged implementation.
