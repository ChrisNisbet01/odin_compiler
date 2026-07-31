# Plan: Compile-time type intrinsics + where-clause support

## Goal

Support the Odin `intrinsics` package's compile-time type-query procs
(`intrinsics.type_is_numeric`, `intrinsics.type_is_float`,
`intrinsics.type_is_quaternion`, `intrinsics.type_is_array`,
`intrinsics.type_base_type`, `intrinsics.type_elem_type`, and the rest of the
"constant type tests" family) and allow calling them in `where` clauses — both
directly (`where intrinsics.type_is_float(T)`) and through constant aliases
(`@private IS_FLOAT :: intrinsics.type_is_float`, then `where IS_FLOAT(T)`).

Also support comma-separated multi-condition `where` clauses with AND
semantics: `where IS_FLOAT(T), !IS_ARRAY(T)`.

Secondary goal (makes the user's `vector_dot` example work end-to-end):
compile-time `when` statements in proc bodies, named return values, `$T/[$N]$E`
vector matching, and `#unroll` loop unrolling.

## Target snippet

```odin
package main

import "core:fmt"
import "intrinsics"

@private IS_QUATERNION :: intrinsics.type_is_quaternion
@private IS_ARRAY       :: intrinsics.type_is_array
@private IS_FLOAT       :: intrinsics.type_is_float
@private IS_NUMERIC     :: intrinsics.type_is_numeric
@private BASE_TYPE      :: intrinsics.type_base_type
@private ELEM_TYPE      :: intrinsics.type_elem_type

scalar_dot :: proc(a, b: $T) -> T
where IS_NUMERIC(T), !IS_ARRAY(T)
{
    return a * b
}

vector_dot :: proc(a, b: $T/[$N]$E) -> E
where ELEM_TYPE(T) == E, IS_NUMERIC(E)
{
    when N == 1 {
        return a.x * b.x
    } else when N == 2 {
        return a.x * b.x + a.y * b.y
    } else when N == 3 {
        return a.x * b.x + a.y * b.y + a.z * b.z
    } else when N == 4 {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
    } else {
        c: E
        #unroll for _, i in a {
            c += a[i] * b[i]
        }
        return c
    }
}

main :: proc() {
    a := #simd [4]f32{1.0, 2.0, 3.0, 4.0}
    b := #simd [4]f32{4.0, 3.0, 2.0, 1.0}
    fmt.println(vector_dot(a, b))
}
```

---

## Phase 1 — Intrinsics stub package + constant aliasing

### 1a. `stubs/base/intrinsics/intrinsics.odin`

New package file:

```
package intrinsics

type_base_type :: proc($T: typeid) -> type ---
type_core_type :: proc($T: typeid) -> type ---
type_elem_type :: proc($T: typeid) -> type ---

type_is_boolean    :: proc($T: typeid) -> bool ---
type_is_integer    :: proc($T: typeid) -> bool ---
type_is_rune       :: proc($T: typeid) -> bool ---
type_is_float      :: proc($T: typeid) -> bool ---
type_is_complex    :: proc($T: typeid) -> bool ---
type_is_quaternion :: proc($T: typeid) -> bool ---
type_is_typeid     :: proc($T: typeid) -> bool ---
type_is_any        :: proc($T: typeid) -> bool ---
type_is_string     :: proc($T: typeid) -> bool ---
type_is_unsigned   :: proc($T: typeid) -> bool ---
type_is_numeric    :: proc($T: typeid) -> bool ---
type_is_ordered    :: proc($T: typeid) -> bool ---
type_is_ordered_numeric :: proc($T: typeid) -> bool ---
type_is_indexable  :: proc($T: typeid) -> bool ---
type_is_sliceable  :: proc($T: typeid) -> bool ---
type_is_comparable :: proc($T: typeid) -> bool ---
type_is_pointer    :: proc($T: typeid) -> bool ---
type_is_multi_pointer :: proc($T: typeid) -> bool ---
type_is_array      :: proc($T: typeid) -> bool ---
type_is_slice      :: proc($T: typeid) -> bool ---
type_is_dynamic_array :: proc($T: typeid) -> bool ---
type_is_map        :: proc($T: typeid) -> bool ---
type_is_struct     :: proc($T: typeid) -> bool ---
type_is_union      :: proc($T: typeid) -> bool ---
type_is_enum       :: proc($T: typeid) -> bool ---
type_is_proc       :: proc($T: typeid) -> bool ---
type_is_bit_set    :: proc($T: typeid) -> bool ---
type_is_bit_field  :: proc($T: typeid) -> bool ---
type_is_simd_vector :: proc($T: typeid) -> bool ---
type_is_matrix     :: proc($T: typeid) -> bool ---
type_has_nil       :: proc($T: typeid) -> bool ---
```

Notes:
- Declarations are poly procs (`$T: typeid`) with `---` bodies. `poly_signature_is_polymorphic`
  returns true, so pass1 marks them `is_polymorphic`, `sem_analyse_procedure_literal`
  early-returns (semantic_analyser.c:811), and IR gen skips them (llvm_ir_generator.c:1084).
  No runtime code is generated.
- No `@(builtin)` needed (and avoided — it would route them into
  `ir_gen_runtime_intrinsic_body`'s unknown-name `LLVMBuildUnreachable` path if ever
  reached; poly skip protects that, but belt-and-braces we just don't set it).
- Skip the intrinsics that carry their own `where` clauses
  (`type_is_matrix_row_major`, union/bit_set/proc/struct variants) for now —
  they parse but we don't evaluate their where clauses.

### 1b. Auto-import `base:intrinsics`

In `sem_pass1_register_top_level_ex` (semantic_analyser.c:1390, alongside the
`base:runtime` auto-import): auto-import `"base:intrinsics"` as a NAMED (non-using)
import so `intrinsics.type_is_float` resolves. Reuse the same import machinery
(`resolve_import_path`, `parse_imported_path`, scope creation, recursive
pass1/pass2/IR-gen processing) that `base:runtime` uses. `is_runtime=true`-style
guard flag (reuse or add `is_intrinsics`) prevents double import; `is_using=false`.

### 1c. Constant-alias detection (pass 2)

`intrinsics.type_is_float` evaluates (sem_evaluate_expr) to a PostfixExpression
whose POSTFIX_MEMBER op gets `op->resolved_symbol` = the intrinsic proc symbol
in the intrinsics package scope (name `"type_is_float"`).

In the `AST_NODE_CONSTANT_DECL` pass2 handler (semantic_analyser.c:2163), after
the value expression is evaluated, detect:
- value is a PostfixExpression (possibly via ExpressionOrStructLit/Expression
  wrappers) whose postfix chain ends in POSTFIX_MEMBER with a `resolved_symbol`
  whose name is in the known-intrinsic set, OR
- value is a bare Identifier that resolves (in scope) to a symbol that is itself
  such an intrinsic-aliasing constant (chained aliases).

When detected, register the alias in a new side table (mirroring the
`poly_register_origin` pattern at polymorphism.c:368):

```c
void poly_register_intrinsic_alias(symbol_t * alias_sym, symbol_t * intrinsic_sym);
symbol_t * poly_get_intrinsic_alias(symbol_t * alias_sym);
```

Both `alias_sym` (e.g. `IS_FLOAT`) and `intrinsic_sym` (e.g. `type_is_float`)
live for the whole compilation; the table is static, keyed by `symbol_t*`.

`sem_analyse_attributes` already handles the bare `@private` form (committed).

---

## Phase 2 — Where-clause evaluator extensions (polymorphism.c)

### 2a. Known-intrinsic registry + dispatch

Add a static registry of intrinsic names → evaluator, e.g.:

```c
static long long poly_eval_intrinsic(SemContext * ctx, char const * name,
                                     TypeDescriptor const * td);
```

Mapping (against `td`, with `TD_KIND_DISTINCT` unwrapped where real Odin does):
- `type_is_boolean`    → basic && strcmp(name,"bool")==0
- `type_is_integer`    → is_integer_kind(td)
- `type_is_rune`       → basic && name=="rune"
- `type_is_float`      → is_floating_kind(td)
- `type_is_complex`    → basic && name starts with "complex" (none registered yet → false)
- `type_is_quaternion` → basic && name starts with "quaternion" (none yet → false)
- `type_is_typeid`     → basic && name=="typeid"
- `type_is_any`        → basic && name=="any"
- `type_is_string`     → basic && name=="string"
- `type_is_unsigned`   → basic && is_unsigned (or integer && unsigned flag)
- `type_is_numeric`    → integer || float || complex || quaternion || matrix || simd_vector
- `type_is_ordered`    → numeric minus complex/quaternion
- `type_is_ordered_numeric` → same as ordered for our type set
- `type_is_indexable`  → array || slice || dynamic_array || string || map || vector || matrix
- `type_is_sliceable`  → array || slice || dynamic_array || string
- `type_is_comparable` → numeric, bool, rune, string, enum, pointer, bit_set, struct/union of comparable... (basic conservative: integer/float/rune/bool/string/typeid/pointer/enum)
- `type_is_pointer`        → TD_KIND_POINTER
- `type_is_multi_pointer`  → TD_KIND_MULTI_POINTER
- `type_is_array`          → TD_KIND_ARRAY
- `type_is_slice`          → TD_KIND_SLICE
- `type_is_dynamic_array`  → TD_KIND_DYNAMIC_ARRAY
- `type_is_map`            → TD_KIND_MAP
- `type_is_struct`         → TD_KIND_STRUCT
- `type_is_union`          → TD_KIND_UNION
- `type_is_enum`           → TD_KIND_ENUM
- `type_is_proc`           → TD_KIND_PROC
- `type_is_bit_set`        → TD_KIND_BIT_SET
- `type_is_bit_field`      → TD_KIND_BIT_FIELD
- `type_is_simd_vector`    → TD_KIND_VECTOR
- `type_is_matrix`         → TD_KIND_MATRIX
- `type_has_nil`           → pointer || multi_pointer || slice || dynamic_array || map || maybe
- `type_base_type`         → unwrap DISTINCT chain, return resulting td's `type_id`
- `type_core_type`         → same as base for now
- `type_elem_type`         → array/slice/dynamic_array/vector/matrix → element_type; pointer/multi_pointer → pointee; maybe → inner; map → value_type; return resulting td's `type_id`
- unknown name            → -1 (can't evaluate)

Bool results are 0/1; type-returning intrinsics return the resulting
descriptor's `type_id` so they compose with `typeid_of(...) == typeid_of(...)`.

### 2b. Postfix-call evaluation in `poly_eval_where_expr`

Add a `case AST_NODE_POSTFIX_EXPRESSION:` in the switch (polymorphism.c:117)
that calls a new `poly_eval_where_call(ctx, node)`:

1. Base unwrap: `children[0]` → down to Identifier (through PrimaryExpression).
2. Postfix ops: `children[1]` → find the FIRST POSTFIX_CALL.
3. Resolve callee to an intrinsic symbol:
   - If there is a leading POSTFIX_MEMBER op and the base identifier is a package
     name (`find_imported_package_by_name`), look the member up in the package scope.
   - Else resolve the base identifier via `scope_find_symbol_entry(current_scope)`;
     if it's a constant with an intrinsic alias (`poly_get_intrinsic_alias`), use it.
     Follow chained aliases.
4. If the callee symbol's name is in the known-intrinsic set, extract the single
   argument: the CALL op's `children[0]` (ARGUMENT_LIST) → child → unwrap
   expression wrappers → resolve via `poly_resolve_type_for_where` (polymorphism.c:24)
   → dispatch `poly_eval_intrinsic`. `typeid_of(...)`/`size_of(...)` args also
   work because `poly_resolve_type_for_where` handles TypeName; for an arg that is
   itself `typeid_of(X)`, resolve X through the same helper.
5. Otherwise return -1.

### 2c. Comma-separated conditions (AND)

In `poly_evaluate_where_clause` (polymorphism.c:272), after extracting the where
Expression, decompose with `sem_collect_comma_chain_args` (sem_context.c:180) and
require EVERY condition to evaluate to non-zero (AND). A condition that cannot be
evaluated returns false (constraint not met) — preserves existing behaviour.

### 2d. Expose the evaluator

Add `poly_eval_where_expr` to polymorphism.h (non-static) so the semantic
analyser can reuse it for compile-time `when` conditions (Phase 3).

**DONE**: `poly_eval_where_expr` is now non-static (polymorphism.h).
`poly_eval_intrinsic` extended with `type_base_type`/`type_core_type`/
`type_elem_type` (return the resulting descriptor's `type_id`). The IDENTIFIER
case falls back to the poly type env (returns `td->type_id`) so
`type_elem_type(T) == E` composes.

---

## Phase 3 — Compile-time `when` statements in proc bodies

`WhenStatement` grammar already supports `when C { } else when C2 { } ... else { }`
(odin_grammar.gdl:519). Currently semantic analysis analyses ALL branches and IR
gen lowers it to a runtime `if`. For poly procs the branches are only valid for
specific specializations (`.x` swizzles on lane-sized vectors), so both phases
must select one branch at compile time.

### 3a. Semantic (semantic_analyser.c:2373)

Iterate children interleaved as [cond, body, cond, body, ..., elseBody?]; evaluate
each condition with `poly_eval_where_expr` (has poly env). Analyse ONLY the first
matching body (push scope, `sem_analyse_compound_statement`, pop). Store the
selected body in `node->metadata`. If no condition evaluates at compile time and
there is no `else`, emit an error (`when` conditions must be compile-time
constants).

### 3b. IR gen

New `ir_gen_when_statement(ctx, node)`:
- For each condition try `ir_gen_evaluate_constant_bool` (llvm_ir_generator.c:3045;
  resolves poly-int `$N` via symbol `has_const_int_val`, registered at
  llvm_ir_generator.c:3588). On success use it; on failure fall back to the
  branch recorded in `node->metadata` by semantic analysis.
- Emit only the selected body (`ir_gen_node` on the CompoundStatement), no
  branch/merge blocks.
- Dispatch `AST_NODE_WHEN_STATEMENT` to it (currently shares
  `ir_gen_if_statement`, llvm_ir_generator.c:2942).

Limitation (documented): at IR-gen time only poly-INT conditions are re-evaluable;
type-based conditions fall back to the semantic selection (correct for single
specialization; last-wins across multiple specializations of the same origin).

**DONE (3a/3b)**: semantic_analyser.c WHEN_STATEMENT handler evaluates conditions
with `poly_eval_where_expr`, analyses ONLY the first matching branch. Selection
stored in a side table (`poly_register_when_selection`/`poly_get_when_selection`,
polymorphism.c) — NOT `node->metadata`, which the AST teardown free()s
(odin_grammar_ast_actions.c:657). New `ir_gen_when_statement` (llvm_ir_generator.c)
re-evaluates conditions via `ir_gen_evaluate_constant_bool` (resolves $N via
`has_const_int_val`); falls back to the side-table selection on `-1`.
`when true`/`when false`/`when N == X`/else chains verified (test_when.odin,
test_when_body.odin). Also fixed `poly_eval_where_expr`'s unwrap loop to descend
through a bare PostfixExpression (empty postfix_ops), so `when true` reaches
BOOL_TRUE.

---

## Phase 4 — Named return values (for `(c: E)`)

- `sem_resolve_procedure_signature` already parses named returns (AST_NODE_NAMED_RETURN
  exists, odin_grammar_ast_actions.c:322). Verify it binds the name.
- Register each named return as a zero-initialized local variable in the body scope
  (semantic + IR gen), typed as its declared type. `return` with no expression in a
  proc with named returns emits the named values.
- `ir_gen_return_statement` / `ir_gen_implicit_return` return the named var values.

Check current named-return status first — may already partially work.

---

## Phase 5 — `$T/[$N]$E` vector matching + `#unroll` (DONE)

### 5a. Vector spec matching (DONE)

`[$N]$E` parses as `AST_NODE_ARRAY_TYPE`. In `poly_unify_poly_idents_in_type`
(polymorphism.c:907) the ArrayType branch only matches `TD_KIND_ARRAY`. Extend it
to also accept `TD_KIND_VECTOR` args (bind `$N` = `as.vector.lane_count`,
element pattern against `as.vector.element_type`). This lets
`vector_dot(a: $T/[$N]$E, b: $T/[$N]$E) -> E` accept `#simd [4]f32` args.

Done: Added `KW_VECTOR` to the element type matching in `poly_unify_poly_idents_in_type`.
Also added `TD_KIND_SLICE`, `TD_KIND_DYNAMIC_ARRAY`, `TD_KIND_MULTI_POINTER`,
`TD_KIND_POINTER`, `TD_KIND_MAYBE` cases.

### 5b. `#unroll` directive (DONE)

- Grammar: added `KwUnroll` lexeme (`"#unroll" IdBoundary`), added to `DirectiveName`.
  `ForStatement` accepts optional leading `Directive?` before `KwFor` — the `#unroll`
  becomes a child node.
- Semantic analyser: skips the leading directive child when processing for-range.
- IR gen: when a for-range has compile-time constant bounds (`ir_gen_evaluate_constant_int`
  on low/high, including poly `$N` via `has_const_int_val`), unroll by emitting the body
  N times with constant induction values. Poly `$N` bounds work via the specialization's
  poly-int registration (llvm_ir_generator.c:3650).
- Added helper `ir_gen_unroll_parse_literal` and `ir_gen_unroll_range_info` to
  compute iteration counts for ranges (handling `TD_KIND_RANGE`, `TD_KIND_VECTOR`,
  `TD_KIND_ARRAY`). Vector/Array unroll binds loop variable to compile-time index.

### Target snippet (DONE)

The full `vector_dot` example from the plan now works. Note: the target snippet
uses `#unroll for _, i in a` — the `_` placeholder is an anonymous iteration
variable (swallow), and `i` is the index. This pattern works for both arrays
and #simd vectors.

---

## Tests

- `tests/test_intrinsics_where.odin` — scalar_dot with IS_NUMERIC(T), !IS_ARRAY(T);
  int/f32/f64; where intrinsics.type_is_float(T) direct form; comma conditions.
- `tests/test_intrinsics_const.odin` — the @private IS_* constant aliases resolve
  and type_base_type/type_elem_type compose with typeid_of/size_of.
- `expected_to_fail/test_intrinsics_where_fail.odin` — calling scalar_dot on a
  string (where rejected) → compile error.
- `tests/test_when_body.odin` — compile-time `when N == 1/2/else` inside a poly
  proc with $N; verify only selected branch is analysed/codegen'd.
- `tests/test_named_return.odin` — `(c: E)` named return.
- `tests/test_vector_dot.odin` — full target example (N=4 simd vector).
- `tests/test_unroll.odin` — `#unroll for` over constant bounds (DONE).
- `tests/test_unroll_poly.odin` — poly `$N` unrolled for-range in `poly_sum` proc
  (DONE). Tests N=5, N=3, N=1.

Run: `cmake --build build` and `bash tests/run_tests.sh`.

## File touch list

- `stubs/base/intrinsics/intrinsics.odin` (new)
- `src/polymorphism.h` / `src/polymorphism.c` (alias table, intrinsic dispatch,
  postfix-call eval, comma AND, expose evaluator, vector spec match)
- `src/semantic_analyser.c` (auto-import intrinsics; alias detection in
  ConstantDecl pass2; when-statement compile-time selection)
- `src/llvm_ir_generator.c` (when-statement IR gen; `#unroll`; named-return IR)
- `src/odin_grammar.gdl` (KwUnroll + ForStatement directive) + regen
  (remember: `touch src/odin_grammar.gdl` after any git stash/pop)
- tests (above)

## Gotchas

- After `git stash`/pop: `touch src/odin_grammar.gdl` before rebuilding.
- Build/test: `cmake --build build`, `bash tests/run_tests.sh`,
  `ODIN_ROOT="$(cd stubs && pwd)" ./build/src/odinc build --file <file>`.
- Where-clause expressions are NOT run through `sem_evaluate_expr` — the where
  evaluator must resolve symbols itself (op->resolved_symbol is NULL on where AST).
- Intrinsic proc symbols have `value.type_info == NULL` (poly early return in
  sem_analyse_procedure_literal) — dispatch on `name`, not on type.
