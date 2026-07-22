## Accomplishments (session 2026-07-18, continued)

### Phase 4.2 Step 3: Extracted statement/control-flow codegen → `ir_gen_statement.c/h`
- **Extracted 9 functions** (defer helpers, statement, control flow) from `llvm_ir_generator.c` into `ir_gen_statement.c`/`ir_gen_statement.h`:
  - Defer: `ir_gen_emit_defers_at_depth`, `ir_gen_emit_defers_from_depth`, `ir_gen_emit_all_defers`, `ir_gen_node_contains_auto_cast`
  - Statement: `ir_gen_return_statement`, `ir_gen_compound_statement`
  - Control flow: `ir_gen_if_statement`, `ir_gen_for_statement`, `ir_gen_switch_statement`
- **7 functions made non-static** with declarations in the new header (called from `llvm_ir_generator.c` dispatch in `ir_gen_node`).
- **Forward declarations removed** from `llvm_ir_generator.c` (the 3 defer forward decls).
- **`llvm_ir_generator.c`**: shrunk from ~6938 → ~3445 lines (-~3493 lines).
- **Net change**: ~659 lines moved (actual extraction), ~3493 removed from main file (the defer/statement/control-flow section).
- **All 155 tests pass**.

## Accomplishments (session 2026-07-22)

### Implemented `where` clause evaluation (Stage 9)
- **Root cause of segfault (implicit function declaration)**: `polymorphism.c` called `sem_resolve_type_expr()` without including `sem_type_resolver.h`. The compiler assumed it returned `int`, truncating the 64-bit pointer to 32 bits (e.g., `0x5e477c85fcd0` → `0x7c85fcd0`). Fixed by adding `#include "sem_type_resolver.h"` to `polymorphism.c`.
- **Root cause of nil bound_type (ARGUMENT_LIST comma-chain issue)**: `poly_build_env_from_args` iterated `arg_list_node->list.children` directly, but the argument list wraps comma-separated expressions in a single `AST_NODE_EXPRESSION` child via chainl1. Fixed by decomposing each ARGUMENT_LIST child through `sem_collect_comma_chain_args()` to extract individual argument expressions. Same fix applied to the argument evaluation loop in `sem_evaluate_expr.c`.
- **Where clause evaluation functions** (already implemented in previous session): `poly_resolve_type_for_where` (unwraps `AST_NODE_TYPE_NAME`, looks up poly env), `poly_eval_typeid_of`, `poly_eval_size_of`, `poly_eval_where_expr` (recursive evaluator for `==`, `!=`, `&&`, `||`, `!`, arithmetic, `typeid_of`, `size_of`), `poly_find_where_clause`, `poly_evaluate_where_clause`.
- **Where clause hooked into `poly_resolve_call`**: After `poly_env_push`, before `sem_analyse_procedure_literal`. Returns NULL on constraint violation (caller decides error vs skip).
- **Tests**: `test_where_clause.odin` (3 tests: `typeid_of(T)==typeid_of(int)`, `typeid_of(T)==typeid_of(f64)`, `size_of(A)==size_of(B)` with matching sizes). `expected_to_fail/test_where_clause_fail.odin` (mismatched `size_of` correctly produces compile error).
- **All 167 tests pass** (164 previous + 2 new regular + 1 new expected-to-fail, minus test_where_clause_fail.odin which replaces previous placeholder).

### Implemented where-clause overload filtering (Stage 10)
- **No code changes needed** — the infrastructure from Stage 9 (`poly_resolve_call` evaluating where clauses + `sem_resolve_overload_bundle_call` loop calling `poly_resolve_call` per candidate with error suppression) already handles this case.
- **Tests**: `test_poly_overload_where.odin` (5 subtests: poly-only bundle `dispatch :: proc{identity_int, identity_f64}` with `where typeid_of(T)==typeid_of(int)` / `typeid_of(T)==typeid_of(f64)` dispatching int vs f64 args; size_of-based dispatch; logical OR where clause). `expected_to_fail/test_poly_overload_where_ambiguous.odin` (two candidates with identical where clauses → ambiguity error).
- **All 169 tests pass** (167 previous + 2 new).

### Verified explicit `$T: typeid` parameter syntax
- **Grammar already works** — `proc($T: typeid, x: T)` parses correctly as `PolyIdent Colon TypePrefix`. No code changes needed.
- **Key finding**: The explicit `$T: typeid` form is for declarations only. Calls must use shorthand form (`identity_explicit(42)`, not `identity_explicit(int, 42)`), because type keywords like `int`, `string`, `f64` don't parse as value expressions in `PrimaryExpression`.
- **Also discovered**: `proc($N: int, arr: [$N]int)` explicit form doesn't register `$N` in body scope (only the shorthand `proc(arr: [$N]int)` works). This is because the explicit `$N: int` is skipped by `poly_build_env_from_args` and the body scope registration doesn't handle the explicit declaration position.
- **Tests**: `test_poly_explicit_params.odin` (6 subtests: `identity_explicit` with int/f64/bool, `first_of` with int/f64, `sum_array` shorthand with `$N`).
- **All 170 tests pass**.

### Implemented polymorphic overload bundle resolution
- **3 bugs fixed** to make `show :: proc{print_int, double_poly}` work with polymorphic candidates:
  1. **Root cause (poly_ident name mismatch)**: `sem_resolve_poly_ident_type` in `sem_type_resolver.c:1377` looked up `poly_env_lookup_type(ctx, "$T")` but the poly env stores entries with name `"T"` (with `$` stripped at `poly_build_env_from_args:336`). Fixed by stripping the `$` prefix before lookup.
  2. **Env binding skipped for `x: $T`**: `poly_build_env_from_args` at `polymorphism.c:331-368` treated ALL `AST_NODE_POLY_IDENT` occurrences as declarations (`$T: typeid` variants that skip `param_idx`), but `x: $T` uses `$T` as a type reference that must be bound to the arg type. Fixed by checking whether the poly ident is the parameter name (declaration) vs. type position (reference).
  3. **Parameter not registered in body scope**: `sem_analyse_procedure_literal` at `semantic_analyser.c:678` did not include `AST_NODE_POLY_IDENT` in the type node detection for parameter registration (unlike `sem_resolve_procedure_signature:473` which did). Fixed by adding `pc->type == AST_NODE_POLY_IDENT` to the `else if` branch.
- **Tests**: `test_polymorphic_overload_bundle.odin` passes (covers exact match `show(42)` → `print_int`, and poly specialization `show(3.14)` → `double_poly{T=float}`).
- **All 160 tests pass**.

## Accomplishments (session 2026-07-21)

### Fixed polymorphic call codegen — `op->resolved_symbol` dispatch priority
- **Root cause**: `ir_gen_postfix_call` in `ir_gen_postfix.c:167-216` used `sym->value.type_info` (the **original** polymorphic type with 0 runtime params — `$T: typeid` and `x: T` both skipped during pass1). The `resolved_symbol` path at lines 197-216 was gated behind `(proc_type == NULL || proc_type->kind != TD_KIND_PROC)`, which was false because the original type IS `TD_KIND_PROC`. The specialization's concrete type was never consulted for call codegen.
- **Fix**: Restructured the dispatch into 3 priorities: (1) `op->resolved_symbol` from semantic analyser (`poly_resolve_call`), (2) scope-based `sym` lookup, (3) `*cur_type` fallback. When the semantic analyser has resolved a polymorphic call to a concrete specialization, that specialization's type and LLVM function value are used directly.
- **Also fixed UAF bugs**: Scope hash-key dangling (`scope_lists.c:74` used caller's transient pointer instead of `strdup`d copy) and immediate scope-free during codegen (`generator_pop_scope` defers `scope_free`; `generator_free_deferred_scopes` called in `main.c:598-600` after codegen).
- **Result**: `test_polymorphic_basics.odin` (and its self-contained `/tmp/test_poly.odin` variant) compiles, links, and executes correctly. **All 158 tests pass**.

## Accomplishments (session 2026-07-18)

### Phase 3.4: Split `ir_gen_node` — 11 inline cases → named function calls
- **Extracted 11 case groups** from the switch into named functions (already done before this session): `ir_gen_cast_expr`, `ir_gen_len_cap_expr`, `ir_gen_offset_of_expr`, `ir_gen_raw_data_expr`, `ir_gen_make_expr`, `ir_gen_delete_expr`, `ir_gen_incl_excl_expr`, `ir_gen_compress_values_expr`, `ir_gen_soa_zip_expr`, `ir_gen_soa_unzip_expr`, `ir_gen_directive`.
- **Replaced 11 case bodies** with direct function calls: CAST_EXPR, LEN_EXPR/CAP_EXPR, OFFSET_OF_EXPR, RAW_DATA_EXPR, MAKE_EXPR, DELETE_EXPR, INCL_EXPR/EXCL_EXPR, COMPRESS_VALUES_EXPR, SOA_ZIP_EXPR, SOA_UNZIP_EXPR, DIRECTIVE/DIRECTIVE_WITH_ARGS.
- **Net change**: 0 lines (functions already extracted; case bodies replaced with 1-line calls).
- **All 155 tests pass**.

### Phase 3.3: Split `ir_gen_postfix_expression` — 6 extracted helpers + dispatch
- **Extracted 6 case groups** from the for-loop switch into separate named helpers: `ir_gen_postfix_call` (returns `bool` for fatal-error early-exit on `"called value is not a procedure"`), `ir_gen_postfix_subscript`, `ir_gen_postfix_member`, `ir_gen_postfix_deref`, `ir_gen_postfix_assertion`, `ir_gen_postfix_slice` (handles both `POSTFIX_SLICE` and `POSTFIX_SLICE_LT`).
- **Dispatch replacement**: The ~960-line switch body replaced with a 10-line switch calling each helper via `(ctx, op, &val, &cur_type)` — each updates state through pointers and the for-loop continues to the next postfix op.
- **Dead code removal**: `result_vec_type = LLVMVectorType(...)` in the swizzle handler (declared, set, never used) was removed in the extraction.
- **Net change**: -16 lines (878 insertions, 894 deletions).
- **All 155 tests pass**.

### Phase 3.2: Split `sem_resolve_type_expr` — switch → dispatch table + 18 extracted functions
- **Step 1**: All 19 case groups (BASIC_TYPE/TYPE_NAME, POINTER_TYPE, ARRAY_TYPE, DISTINCT_TYPE, SLICE_TYPE, MULTI_POINTER_TYPE, DYNAMIC_ARRAY_TYPE, MAP_TYPE, BIT_FIELD_TYPE, BIT_SET_TYPE (with `#PUSH` macro), PROCEDURE_SIGNATURE, ENUM_TYPE, STRUCT_TYPE, UNION_TYPE, SOA_TYPE, MAYBE_TYPE, TUPLE_TYPE, VECTOR_TYPE, IDENTIFIER) extracted into named functions (`sem_resolve_<type>_type()`).
- **Handled `#PUSH` macro in BIT_SET_TYPE**: The local `#define PUSH`/`#undef PUSH` macro was moved into the extracted `sem_resolve_bit_set_type` function body (file-scope preprocessor directive inside a C function is valid).
- **Collision fix**: `AST_NODE_PROCEDURE_SIGNATURE` was NOT extracted — the dispatch table points to the existing `sem_resolve_procedure_signature` function (defined elsewhere in the file), avoiding a duplicate name.
- **Step 2**: Switch replaced with `AST_NODE_COUNT`-sized function pointer lookup table `sem_resolve_type_dispatch[]`. `sem_resolve_type_expr` is now 10 lines of dispatch logic.
- **Net change**: -78 lines (933 insertions, 1011 deletions).
- **No regressions**: 2 pre-existing failures (`test_hash_type.odin`, `test_nested_proc.odin` — confirmed failing before extraction too).
- **Key learning**: The existing `sem_resolve_procedure_signature` at line 968 already handles `PROCEDURE_SIGNATURE` type resolution — the switch case was a duplicate that needed to delegate, not extract.

## Accomplishments (session 2026-07-17)

### Fixed tuple type codegen — 6 interconnected bugs resolved

1. **Missing `llvm_type`**: `TD_KIND_TUPLE` handler in `get_or_create_tuple_type` did not set `llvm_type` (was NULL), causing segfault in `LLVMBuildAlloca`. Fixed by computing `LLVMStructType(field_llvm_types, ...)`.
2. **Composite types list**: `TD_KIND_TUPLE` was missing from the IR generator's composite types detection, so identifier loading bypassed alloca pointer retrieval (treated as integer type).
3. **Unexecuted semantic branch**: `AST_NODE_ENUM_TYPE` handler condition `child->type == AST_NODE_TUPLE_TYPE` was wrong (should be `child_type->type == AST_NODE_TUPLE_TYPE`), causing the tuple-in-enum branch (never reached before) to silently skip, leaving `underlying_type` NULL.
4. **No dedup for tuple types**: `get_or_create_tuple_type` always created a new type on every call, so identical `(int, int)` in different locations got different type IDs. Added deduplication via `type_name_to_descriptor` hash table with `make_tuple_type_name` hash key.
5. **test_tuple.odin**: Was checking only `result == 0` (which passed even with wrong values). Extended to check both tuple element values via string length and integer comparison.
6. **IR gen for multi-return destructuring**: `AST_NODE_MULTI_RETURN_VARIABLE_DECL` handler was failing for tuples. Fixed by ensuring tuple variables in `ir_gen_variable_decl` get their `llvm_type` from `var_type->llvm_type`.

### Implemented `soa_zip` and `soa_unzip` expressions

- **Grammar**: Added `KwSoaZip`/`KwSoaUnzip` lexemes, `SoaZipExpr`/`SoaUnzipExpr` rules, added to `UnaryExpression` and `AllReservedWords`. Fixed grammar bug — `(Comma Expression)*` doesn't work because `Expression` (via `chainl1` with `Comma`) already consumes commas; switched to `ArgumentList` which produces a comma-chained `Expression` tree.
- **AST**: Added `AST_NODE_SOA_ZIP_EXPR`/`AST_NODE_SOA_UNZIP_EXPR` enum entries, actions, node names.
- **Semantic analyser**: `soa_zip` validates all arguments are slices, creates SOA struct type with `_0`, `_1`, ... field names. `soa_unzip` validates operand is SOA type, resolves to tuple of element types.
- **IR generator**: `soa_zip` extracts `data` pointers and `len` from each slice, finds minimum length, builds SOA struct with truncated slices. `soa_unzip` extracts fields from SOA struct and builds result tuple.
- **Comma-chain arg collection**: Added `sem_collect_comma_chain_args()` and `ir_gen_collect_comma_chain_args()` helpers that walk left-associative comma-chain Expression trees.
- **Tests**: `test_soa_zip.odin` (15 subtests: slice truncation, field access, soa_unzip round-trip).
- **All 155 tests pass** (154 previous + 1 new).

### Key learning
In PEG grammars with `chainl1(Expression, Comma)`, the `Expression` rule itself consumes commas, so using `(Comma Expression)*` after an `Expression` never captures additional arguments. Use `ArgumentList` (which wraps a single `Expression?`) and walk the comma-chain tree to extract individual arguments.

## Accomplishments (session 2026-07-15)

### Implemented `#simd [N]T` vector types with swizzle and subscript
- **Grammar**: Added `KwSimd` lexeme (`#simd` with IdBoundary), `VectorType` rule (`KwSimd LBracket IntegerLiteral RBracket TypePrefix`). Moved `VectorType` before `SoaType` in `TypePrefix` to fix PEG ordering issue (SoaType's `Directive = lexeme("#" DirectiveName)` split `#simd` into `#` + `simd`).
- **AST**: Added `AST_NODE_VECTOR_TYPE` enum, action, node name.
- **Type descriptors**: Added `TD_KIND_VECTOR` with `lane_count`, `element_type`, `llvm_type = LLVMVectorType(...)`.
- **Semantic analyser**: `AST_NODE_VECTOR_TYPE` handler in `sem_resolve_type_expr` calls `get_or_create_vector_type`. Swizzle detection in POSTFIX_MEMBER: validates field name against {x,y,z,w,r,g,b,a} sets (no mixing), resolves single-component to element type, multi-component to new vector type. `TD_KIND_VECTOR` added to subscriptable types in POSTFIX_SUBSCRIPT.
- **IR generator**: Added `TD_KIND_VECTOR` to composite types list (auto-load exclusion). Rvalue swizzle: single → `ExtractElement`, multi → `ShuffleVector`. Rvalue subscript: `ExtractElement`. Lvalue subscript: error (read-modify-write pattern not yet implemented).
- **Vector lvalue subscript & compound assignment**: Added `ir_gen_vector_elem_assign()` helper that detects vector subscript LHS in assign statements. Replaces the error path with load → `InsertElement` → store pattern. Handles compound assignment (`+=`, `-=`) with proper RHS → element type coercion. Called from both `ir_gen_assign_expression` and `ir_gen_assign_statement`. Swizzle lvalue (`v.x = val`) still pending.
- **Key fix**: Removed dead code (`LLVMConstNamedStruct(LLVMStructType(mask_vals, ...))`) that called `LLVMStructType` with `LLVMValueRef*` instead of `LLVMTypeRef*`, causing segfault in `llvm::StructType::get`.
- **Tests**: `test_vector_type.odin`, `test_vector_swizzle.odin`, `test_vector_swizzle_values.odin`, `test_vector_subscript.odin`. Expected failures: `test_vector_swizzle_mixed.odin`, `test_vector_swizzle_oob.odin`.
- **All 149 tests pass** (no regressions).

### Implemented `expand_values` and `compress_values` built-in procedures
- **Grammar**: Added `KwExpandValues`/`KwCompressValues` lexemes, `ExpandValuesExpr`/`CompressValuesExpr` rules, added to `UnaryExpression`/`AllReservedWords`.
- **AST**: Added `AST_NODE_EXPAND_VALUES_EXPR`/`AST_NODE_COMPRESS_VALUES_EXPR` enum entries, actions, node names.
- **Semantic analyser**: `EXPAND_VALUES_EXPR` validates operand is struct/array, sets `resolved_type` to aggregate type. `COMPRESS_VALUES_EXPR` resolves target type, validates field/value count match, evaluates value children.
- **IR generator — `expand_values` in call context**: `ir_gen_collect_single_arg()` helper detects `EXPAND_VALUES_EXPR` after unwrapping expression chain (single-child wrappers only), loads aggregate, extracts fields via `ExtractValue`. Used by `ir_gen_collect_call_args` for the rightmost operand and single-expression fallback.
- **IR generator — standalone `expand_values`**: Returns the aggregate value itself (struct/array value).
- **IR generator — `compress_values`**: Creates `LLVMGetUndef` of target type, inserts each coerced value via `InsertValue`.
- **Bug fix**: `ir_gen_variable_decl` (line 1388) — added `var_type->kind == TD_KIND_BASIC` guard before accessing `as.basic.name` union member; was reading garbage on non-basic types.
- **Tests**: `test_expand_values.odin`, `test_compress_values.odin`. Both pass (struct + array variants).
- **All 151 tests pass** (+2 new, 0 regressions).

## Accomplishments (session 2026-07-16)

### Implemented vector swizzle lvalue assignment
- **Single-component** (`v.x = val`) and **multi-component** (`v.xy = val`, `v.xyzw = val`, non-contiguous like `v.xz`, partial like `v.yw`) swizzle lvalue assignment for `#simd` vectors. Extended `ir_gen_vector_elem_assign` to handle `POSTFIX_MEMBER` (swizzle) in addition to `POSTFIX_SUBSCRIPT` on vector types. Uses `ExtractElement`/`InsertElement` per-lane merge pattern instead of `ShuffleVector` to avoid LLVM's mismatched vector-length operand restriction. Compound assignment (`+=`, `-=`) works for both forms. RHS vector loaded from alloca pointer via `member_op->resolved_type->llvm_type` when needed (avoids opaque-pointer `LLVMGetElementType` NULL issue).
- **Tests**: `test_vector_swizzle_lvalue.odin` (39 subtests). All 152 tests pass.

### Fixed bounds-check PHI node predecessor bug
- **Root cause**: Three PHI-construction sites in `ir_gen_logical_short_circuit` (`src/llvm_ir_generator.c:676,682`) and `ir_gen_or_else` (two variants, lines 2373 and 2402) captured the RHS-starting block *before* evaluating the RHS subtree. When the RHS contained a bounds-checked subscript (`arr[i]`, `s[i]`, `str[i]`), `ir_gen_emit_bounds_check` split that starting block by appending `bc.cont`/`bc.trap` blocks — the unconditional `Br(merge_bb)` actually originated from `bc.cont`, but the PHI still listed the stale pre-split `rhs_bb`, producing a CFG/PHI mismatch that LLVM's FastISel crashes on.
- **Fix**: Re-capture `LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(ctx->builder)` *after* RHS evaluation (mirroring the existing correct pattern in ternary at lines 2489/2497), and use `rhs_end_bb` as the PHI's incoming block instead of the stale `rhs_bb`. Small surgical change: 6 added lines plus 3 swapped variable references across 3 sites.
- **Tests**: `test_bounds_check_short_circuit.odin` (31 subtests covering all three sites: logical `||`, logical `&&`, mixed `||`+`&&` chains, or_else integer variant, or_else Maybe variant, or_else pointer variant, ternary no-regression). Sanity-checked: without the fix, the test triggers the exact FastISel crash reported in the historical notes (`FastISel::handlePHINodesInSuccessorBlocks`, segfault). With the fix, all 32 subtests pass and all 153 tests in the suite pass.
- **No regressions**: previously-passing tests unaffected — `test_logical_short_circuit.odin` and `test_bounds_check.odin` exercise these paths but always with non-subscript RHS, so they never split `rhs_bb` before this fix was applied.

## Accomplishments (session 2026-07-13)

### Implemented `distinct` type creation with type isolation
- **`create_distinct_type()`** (`type_descriptors.c`/`.h`): Allocates a new `TD_KIND_DISTINCT` descriptor with same LLVM layout as base, unique hash (no dedup).
- **`is_integer_kind()` / `is_floating_kind()`**: Unwrap `TD_KIND_DISTINCT` to base before checking, so distinct numeric types accept untyped literals.
- **`sem_types_assignable()` / `sem_check_assignment()`**: Enforce distinct-type assignment rules (only assign to same distinct type, or from untyped literal); all other assignments use old lax behaviour.
- **Fixed `AST_NODE_VARIABLE_DECL`**: Added `child->type == AST_NODE_IDENTIFIER` to type-node classification so `x: MyInt` and `x: Handle` (type alias) both resolve correctly.
- **Tests**: `test_distinct.odin` (6 subtests), `test_distinct_assign_base.odin` (expected fail), `test_distinct_to_base.odin` (expected fail). All pass.

### Fixed `i128`/`u128` regression
- **Root cause**: `i128`/`u128` not in `BasicType` grammar rule → parsed as `Identifier` → the new `child->type == AST_NODE_IDENTIFIER` check made them match as type nodes, but `sem_resolve_type_expr` for `AST_NODE_IDENTIFIER` only checked scope (not type registry) → returned NULL → "undeclared identifier".
- **Fix**: Added `KwI128`/`KwU128` lexemes to `BasicType` rule + `AllReservedWords` in grammar; added `get_basic_type_by_name` fallback in `sem_resolve_type_expr` for robustness.
- **All 120 tests pass** (previously 120 passing).

### Implemented `type_info_of(T)` runtime type info
- **Grammar**: Added `KwTypeInfoOf` lexeme, `TypeInfoOfExpr` rule, updated `UnaryExpression` and `AllReservedWords`.
- **AST**: Added `AST_NODE_TYPE_INFO_OF_EXPR` enum, action, node name.
- **Type descriptors**: Added `type_info` struct `{i64 size, i64 align, i64 id, i64 kind}` as a compiler-internal type, registered in `register_builtin_context_types`. Added getter functions for the opaque `TypeDescriptors` struct.
- **Semantic analyser**: `AST_NODE_TYPE_INFO_OF_EXPR` resolves operand type and returns `^type_info` pointer type.
- **IR generator**: `ir_gen_get_or_create_type_info_global()` lazily creates `internal unnamed_addr constant` globals per unique type, deduplicated by `type_id`. `.id` field stores the type's hash. Fixed signed/unsigned comparison bug (`td->type_id >= 0` truncated high-bit-set hashes to 0).
- **Tests**: `test_type_info_of.odin` (19 subtests covering size, id, pointer comparison, kind). **All 121 tests pass**.

## Accomplishments (session 2026-07-12, continued)

### Implemented chained member access with reserved keyword field names
- **Grammar fix**: Added `FieldName = lexeme(identifier) @AST_ACTION_IDENTIFIER` rule. Changed `PostfixOpMember = Dot Identifier` → `PostfixOpMember = Dot FieldName`. This allows reserved keywords (`len`, `cap`, `type`, …) as field names after a dot, since `Identifier` excludes reserved words via `not(AllReservedWords)`.
- **IR gen rvalue POSTFIX_MEMBER**: Added GEP-based field extraction for `string .len` / `.data`, `slice .len` / `.data`, `dynamic_array .len` / `.cap` / `.data`, and `array .len`.
- **IR gen identifier loading**: Added `TD_KIND_BASIC` with name `"string"` to composite types check, so string variables return their alloca pointer (needed for GEP in member access).
- **IR gen auto-load exclusion**: Prevented auto-loading string pointers (the pointer is needed for subsequent GEP).
- **IR gen pointer auto-dereference (rvalue)**: `p.field` now works — `val` is already the pointer value (struct address), just update `cur_type` to pointee without loading.
- **Tests**: `test_chained_member.odin` updated with 11 subtests covering string `.len`/`.data`, array `.len`, `Maybe.value`, chained `loc.file.len`/`loc.file.data`, and pointer deref `p.line`/`p.column`/`p.file.len`.
- **All 115 tests pass** (117 total, 2 pre-existing baseline failures: `test_file_io`, `test_foreign_strings`).

## Accomplishments (session 2026-07-12)

### Implemented `#assert[expr]` compile-time assertions
- **Fixed semantic analyser**: Replaced premature `break` loop in `AST_NODE_DIRECTIVE_WITH_ARGS` handler with proper single-expression enforcement (error on 0 or multiple expressions). The expression is now fully evaluated and checked as a compile-time boolean constant.
- **Added top-level `#assert` support**: Added `AST_NODE_DIRECTIVE_WITH_ARGS` handler in `sem_pass1_register_top_level_ex` so `#assert` works outside procedure bodies too.
- **Updated tests**: Fixed `test_assert_fail.odin` to use void main (so `#assert` failure is the actual reason). Added `test_assert_toplevel.odin` (passing top-level assertions) and `test_assert_toplevel_fail.odin` (expected-to-fail top-level assertion).
- **All 117 tests pass**. Works with arithmetic, boolean, comparison, bitwise, `typeid_of`, `size_of`, `align_of`, and `offset_of` expressions.

## Accomplishments (session 2026-07-11)

### Refactored print_string/print_byte/int_to_string into intrinsic-based runtime package
- **Removed special grammar rules, AST nodes, and lexer tokens** for `print_string`, `print_byte`, `int_to_string`. No more `KwPrintString`, `PrintStringExpr`, etc. The grammar, semantic analyser, and IR generator no longer contain any builtin-specific code for these functions.
- **Created `stubs/core/runtime/runtime.odin`**: New prelude package with `---` bodyless declarations for `print_string(fd: int, str: string)`, `print_byte(fd: int, b: u8)`, `int_to_string(i: int) -> string`.
- **Auto-import mechanism**: `sem_pass1_register_top_level_ex` now auto-imports `core:runtime` as an implicit `using` import for every file. The runtime package is resolved, parsed, analysed, and codegen'd like a normal import.
- **Intrinsic body generation**: `ir_gen_top_level_decl` detects known runtime function names and calls `ir_gen_runtime_intrinsic_body()` to emit LLVM IR bodies (inline syscall for print_string/print_byte; digit loop for int_to_string). This replaces the old special-case AST node handling.
- **Benefits**: The compiler no longer has special-case logic for these 3 functions — they're regular Odin procedures with `---` declarations. Adding new intrinsics is a matter of adding a declaration in `core:runtime` and a case in `ir_gen_runtime_intrinsic_body()`.
- **All 106 tests pass**.

### Converted `os.exit()` from libc call to runtime intrinsic inline syscall
- **Added `os_exit` to `stubs/core/runtime/runtime.odin`**: Bodyless declaration `os_exit :: proc(code: int) ---` alongside print_string/print_byte/int_to_string.
- **Added `os_exit` to intrinsic detection**: `ir_gen_is_runtime_intrinsic()` and `ir_gen_runtime_intrinsic_body()` handle `os_exit` — emits inline syscall via LLVM inline asm (`mov rax, 60; syscall`), same pattern as print_string/print_byte.
- **Rewrote `stubs/core/os/os.odin`**: Changed from `foreign libc { exit :: proc "c" (code: int) --- }` to a plain Odin procedure that calls `os_exit(code)` — no more `foreign libc` dependency.
- **Fixed inline asm constraint length**: Length parameter (33) must match the constraint string exactly; off-by-3 caused truncated constraint `~{r1` and linker crash.
- **Remaining `foreign libc` callers**: `stubs/src/mem/mem.odin` (malloc/free) and `stubs/src/os/os.odin` (exit/getenv/system) — both are dead code (never imported by any test or real program).
- **All 106 tests pass**.

### Deleted dead `stubs/src/` directory
- **Identified dead code**: `stubs/src/os/os.odin`, `stubs/src/mem/mem.odin`, `stubs/src/builtin/builtin.odin`, `stubs/src/intrinsics/intrinsics.odin`, `stubs/src/runtime/runtime.odin` — no resolution path reaches them.
- **Updated `test_stub_import.odin`**: Removed dead `import "mem"` that only worked through the deleted path.

### Implemented low-hanging fruit features
- **`#no_bounds_check` directive grammar**: Added `KwNoBoundsCheck` lexeme, added to `DirectiveName` alternatives. No-op at IR level (bounds checking not yet implemented). Test: `test_no_bounds_check.odin`.
- **`#partial switch` test**: Grammar already supported `Directive?` on `SwitchStatement`; added test verifying switch without default compiles and runs. Test: `test_switch_partial.odin`.
- **`#soa` standalone directive**: Grammar changed to accept `Directive | DirectiveWithArgs` in `SoaType`. Semantic analyser handles `#soa` without `[N]` by creating slice-backed SOA type (each field `T` → `[]T`). Test: `test_soa_simple.odin`.
- **`odinc run` command**: New subcommand that compiles, links, and executes in one step using temp files (cleaned up after execution). Reports exit code.
- **Housekeeping**: Updated `unsupported_features.md` — moved `typeid_of(T)`, `bit_set[u32]`, `contextless` from "To Do" to "Recently Added"; marked `#partial`, `#no_bounds_check` as `✅ GRAMMAR DONE`.
- **All 109 tests pass**.

## Accomplishments (session 2026-07-09)

### Phase 6: Extended formatting, escape sequences, `%u`
- **Extended escape sequences in `ir_gen_string_literal`**: Added `\a` (0x07), `\b` (0x08), `\e` (0x1B), `\f` (0x0C), `\v` (0x0B), `\'` (0x27), `\0` (0x00), `\xNN` (hex escape). Split dispatch to pass `process_escapes` flag — raw strings (backtick) skip escape processing.
- **Rune literal escape handling**: Rewrote `ir_gen_rune_literal` to handle all escape sequences instead of broken `strtoull` fallback.
- **`%u` format specifier**: Added to `printf` in `stubs/core/fmt/fmt.odin`. Delegates to `print_value` for proper unsigned type dispatch.
- **Extended `print_value`**: Added `u16`, `u32`, `u64` cases; `println` now uses `print_value` for unified type dispatch. `u8` and `byte` updated — `byte` uses `print_byte`, `u8` uses `int_to_string`.
- **Tests**: `tests/test_escape.odin` (47 subtests covering all escapes, hex, mixed, raw strings). `test_fmt.odin` extended with `%u` and unsigned type coverage.
- **Cleaned up `notes/unsupported_features.md`**: Removed supported features, ordered remaining by complexity.
- **All 101 tests pass**.

### Implemented Maybe(T) optional type
- **Grammar**: Added `KwMaybe` keyword, `MaybeType` rule (`KwMaybe LParen TypePrefix RParen`), added to `TypePrefix`/`AllReservedWords`.
- **AST**: Added `AST_NODE_MAYBE_TYPE` enum entry, action, and node name.
- **Type descriptors**: Added `TD_KIND_MAYBE` with `get_or_create_maybe_type` — LLVM layout `{i64 tag, T payload}`.
- **Semantic analyser**: Resolves `Maybe(T)` type, `.value` member access (returns inner type), `x.(T)` type assertion (validates target matches inner type), `or_else` result type unwraps `Maybe(T)` → `T`.
- **IR generator**: 
  - Lvalue `.value`: GEP to field 1, bitcast to inner type pointer. 
  - Rvalue `.value`: GEP + load.
  - `or_else`: tag check (field 0 != 0 → none → RHS, else payload), phi-based merge.
  - Type assertion: tag check + trap on fail, payload extraction via GEP.
  - Variable decl: `none` init sets tag=1; implicit `T→Maybe(T)` wrapping stores payload.
  - Fixed: `init_contains_none` helper for detecting `none` inside `AssignExpression` wrapper.
  - Fixed: `TD_KIND_MAYBE` added to composite types list so identifiers return pointer.
  - Fixed: `LLVMBuildStore` NULL guard for general case.
- **Fixed semantic analyser's `or_else`**: Now unwraps `Maybe(T)` to inner type for result type.
- **Fixed `is_type_node`**: Added `AST_NODE_MAYBE_TYPE`.
- **All 86 tests pass** (test_maybe.odin added with 5 subtests).

## Accomplishments (session 2026-06-19)

**Morning session:**
- **Implemented `in`/`not_in` operators**: Rewrote `ir_gen_in_expression` with clean LLVM control flow (entry → loop → body → incr → found/notfound → merge with phi). Fixed GEP issue with slice structs by switching from GEP to Load+ExtractValue approach. Both arrays and slices work as RHS containers. Tests cover found/not-found for arrays and slices in both `if` conditions and assignments.
- **All 38 tests pass** (test_in.odin added with 11 subtests).

**Afternoon session:**
- **Implemented for-range loop codegen (`for i in expr { body }`)**: Discovered that grammar rules without `@AST_ACTION` annotations flatten their children into the parent (confirmed in `easy_pc_ast.c:341-360`). For `for i in 0..10 { body }`, the `ForStatement` handler receives `[Identifier("i"), Expression(range), CompoundStatement(body)]`. The semantic analyser detects for-range (first child is raw `AST_NODE_IDENTIFIER`, range expression resolves to `TD_KIND_RANGE`), then declares the loop variable as `i64` in the loop scope. The IR generator emits: entry (eval range, extract low/high from struct via alloca+GEP+load) → init loop var → cond (cmp < high) → body → inc (loop var++) → cond. Continue target is the increment block; break target is the end block. Tests cover both half-open (`0..<10`) and inclusive (`1..5`) ranges.
- **Implemented two-variable for-range (`for i, val in expr { body }`)**: Extended the for-range detection to collect all identifier children into an array. The IR generator allocates separate storage for each variable, initializes all to the range low value, and increments all on each iteration. Both variables receive the same loop value (for range expressions). Tests cover `for i, val in 0..<10`.
- **All 40 tests pass**.

## Accomplishments (session 2026-06-21)
- **Implemented `bit_field` type**: Added `KwBitField` lexeme, `BitFieldType`/`BitFieldField`/`BitFieldFieldList` grammar rules with AST actions. Semantic analyser resolves bit_field types (computes total bits, picks i8/i16/i32/i64 backing, deduplicates via `get_or_create_bit_field_type`). IR generator allocates backing storage, handles member access (read field: lshr+and; write field: read-modify-write with mask/shift/or). Added `TD_KIND_BIT_FIELD` to type descriptor union with `bit_field_field_info` array.
- **Fixed grammar bug**: `BitFieldFieldList` used `Semicolon` delimiter instead of `Comma` (causing parse error at the variable declaration colon, since the whole `bit_field { ... }` parsed as just the identifier leaving unparseable tokens).
- **Fixed `ir_gen_bit_field_write` unwrapping**: `is_expression_wrapper_type` includes `AST_NODE_POSTFIX_EXPRESSION`, causing the while loop to over-unwrap past PostfixExpression into its children. Added `lhs_expr->type != AST_NODE_POSTFIX_EXPRESSION` guard.
- **Fixed zero-initialization**: Odin semantics require all local variables to be zero-initialized by default (`x: T` = `x: T = 0`). Added `LLVMBuildStore(LLVMConstNull)` after `LLVMBuildAlloca` in `ir_gen_variable_decl` to prevent garbage reads from uninitialized bit_field backing storage.
- **All 45 tests pass** (test_bit_field.odin added with 10 subtests covering basic write/read, field isolation, multi-field, overwrite, and 64-bit backing).

## Accomplishments (session 2026-06-22)
- **Fixed `incl`/`excl` pointer field access bug**: Both `semantic_analyser.c` and `llvm_ir_generator.c` used `ptr_type->element_type` instead of `ptr_type->pointee` (the latter is the correct field for `TD_KIND_POINTER`). Fixed both locations.
- **Fixed bit_set value loading**: `ir_gen_identifier` treated `TD_KIND_BIT_SET` as a "composite type" that returns the alloca pointer instead of loading the integer backing value. This caused `LLVMBuildNot` (in the `-`/AND-NOT operator) to crash on pointer types. Removed `TD_KIND_BIT_SET` from the composite type list so bit_set variables are properly loaded as integer values.
- **Implemented range-based bit_set (`bit_set[0..<32]`)**: Added `AST_NODE_BIT_SET_RANGE` node type, `BitSetRange` grammar rule (`LogOrExpression (DotDotLt | DotDot) LogOrExpression`), `DEFINE_TERMINAL_ACTION` to capture range text, and semantic handling that extracts low/high bounds, determines inclusive/exclusive from the text, calculates bit count, and selects the smallest backing integer (u8/u16/u32/u64).
- **All 49 tests pass** (test_incl_minimal.odin, test_bit_set_ops.odin with 12 subtests, test_bit_set_range.odin with 5 subtests added).
- **Saved CLI redesign analysis** to `cli_redesign_notes.md` for future reference.

## Accomplishments (session 2026-06-23)
- **Phase 4: Caller context threading**: Modified `AST_NODE_POSTFIX_CALL` in `ir_gen_postfix_expression` to prepend the caller's context alloca as the first arg to ODIN-convention calls. Falls back to a fresh zero-initialized context alloca when `context` is not in scope (top-level init code).
- **Phase 5: Entry point wrapper**: `ir_generate()` detects `main` with hidden context param, renames it to `__odin_main` (private linkage), creates C-compatible `int main()` that allocates zero-initialized Context, calls `__odin_main(ctx_ptr)`, truncates `i64`→`i32`, returns.
- **Fixed LLVM 18 opaque pointer issue**: Used `LLVMGlobalGetValueType()` instead of `LLVMTypeOf()` to get function type from a function value (LLVM 15+ opaque pointers make function value types return `ptr`, not function type).
- **All 49 tests pass**.

## Accomplishments (session 2026-06-24)
- **Fixed foreign block test**: `test_foreign.odin` had a type mismatch — `abs` returns `i32` but `main` returns `int` (i64). Added `cast(int) (result - 5)`.
- **Fixed implicit integer coercion in binary expressions**: `ir_gen_binary_expression` now inserts `LLVMBuildIntCast2` when RHS type differs from LHS type for arithmetic/comparison ops (skips `in`/`not_in`/`range` where types naturally differ). Fixes the `sub i32, i64` crash with foreign libc calls.
- **All 55 tests pass**.

## Accomplishments (session 2026-06-29)
- **Implemented `when` declarations**: Added grammar support (`WhenBody` rule with `@AST_ACTION_WHEN_BODY`, `WhenDecl` updated). Semantic analyzer pass 1 registers decls inside matching when/else branches; pass 2 analyzes bodies. IR generator evaluates conditions at compile time with expression-chain unwrapping, processes matching bodies, emits non-procedure constants via `ir_gen_top_level_decl` by evaluating and storing LLVM values in the symbol table.
- **Fixed `sem_evaluate_constant_bool`/`ir_gen_evaluate_constant_bool`**: Added expression-chain unwrapping (including `POSTFIX_EXPRESSION` which has 2 children) so `when true`/`when false` conditions are properly evaluated instead of falling through switch with UB.
- **Fixed `ir_gen_top_level_decl`**: Extended to handle non-procedure top-level constants (`X :: 100`) by evaluating the value expression and storing the LLVM constant in the symbol table via `generator_add_symbol`. Previously only handled procedure literals.
- **All 63 tests pass** (test_when_decl.odin added with `when true`/`when false`/`when false...else` branches).

## Accomplishments (session 2026-07-01)

### SOA structs and `#soa` directive fix
- **Implemented SOA structs (`struct #soa { x: T; y: U }`)**: Added `TD_KIND_SOA` type descriptor with slice-backed fields. Semantic analyser transforms each field type `T` → `[]T` for SOA structs. IR generator allocates struct-of-slices LLVM type and handles field access via GEP.
- **Fixed `#soa` directive detection**: The `lexeme("#" DirectiveName)` parser captures trailing whitespace in its semantic content, producing text `"#soa "` (len 5) instead of `"#soa"` (len 4). Changed `strcmp(child->text, "#soa")` to `strstr(child->text, "#soa") != NULL` in `semantic_analyser.c:817`.
- **All 65 tests pass** (test_soa.odin with 3 `len(s.x)` calls on SOA struct fields).

### Package imports (session 2026-07-01 continued)
- **Named imports (`import alias "path"`)**: Added `AST_NODE_IMPORT_NAMED` grammar rule (`KwImport Identifier StringLiteral`). Semantic analyser extracts alias name from `children[0]` and overrides `pkg->package_name`. Tested with `test_import_named.odin`.
- **Using imports (`import using "path"`)**: Added `AST_NODE_IMPORT_USING` grammar rule (`KwImport KwUsing StringLiteral`). Semantic analyser runs pass1/pass2 on the imported package then copies all symbols from the package scope into the current scope via `generic_hash_table_iterate`. Tested with `test_import_using.odin`.
- **Recursive imports**: Tested chain `main → a → b` — three-level transitive imports work correctly.
- **Import cycle detection**: Added `import_stack` to `SemContext` (dynamic array of resolved paths). Before parsing an import, checks if the resolved path is already in the stack; if so, prints error and aborts. Push/pop wraps each import's parse+analyse cycle. Tested with `expected_to_fail/test_import_cycle.odin` (a↔b cycle).
- **ODIN_ROOT resolution hardening**: `resolve_odin_root` now calls `realpath` on the exe path first to resolve symlinks and relative paths before computing `<exe_dir>/../..`.
- **All 69 tests pass**.

## Accomplishments (session 2026-07-02)

### Import codegen cross-package function calls
- **Fixed build**: Added `import_using_copy_symbol` as a static function in `llvm_ir_generator.c` to resolve linker error (previously only existed as `static` in `semantic_analyser.c`).
- **Implemented cross-package function calls**: Package-qualified member access (`pkg.func()`) now works end-to-end. The semantic analyser's `POSTFIX_EXPRESSION` handler recognizes package-qualified names and resolves members via the package scope. The IR generator's `POSTFIX_MEMBER` handler reads `op->resolved_symbol` when `cur_type` is NULL, and `POSTFIX_CALL` falls back to `cur_type` when the function isn't in the local scope. Tested with `test_import_func_call.odin` (calls `test_import_helper.helper_func(41)` → returns 42).
- **`import using` with function calls**: Works via the symbol re-copy loop in `ir_generate()` that refreshes LLVM values after import codegen. Tested with `test_import_using_func_call.odin` (calls `helper_func(41)` directly).
- **Fixed test bug**: `assert(...)` is a compile-time directive `#assert[...]`, not a runtime function. Fixed both new tests to use return-value checking instead.
- **All 72 tests pass**.

## Accomplishments (session 2026-07-03)

### Better error reporting with source locations
- **Populated `source_data.view` in all AST nodes**: Modified `make_node`, `make_node_base`, and `ast_action_struct_field_action` in `odin_grammar_ast_actions.c` to call `epc_cpt_node_get_input_view(node)` and store line/column info on every AST node during construction.
- **Added file path tracking to semantic errors**: Added `file_path` field to `SemError` struct, updated `sem_error_list_add` signature to take a file path parameter. All 25+ call sites in `semantic_analyser.c` updated to pass `ctx->source_file_path`.
- **Per-file error tracking for imports**: During import processing, `ctx->source_file_path` is temporarily swapped to `pkg->source_path` so errors from imported files correctly show the imported file's path.
- **Improved `sem_error_list_print`**: Now outputs `error: <file>:<line>:<col>: <message>` instead of the bare `Semantic error: <message>`.
- **Same improvements to IR gen errors**: Added `file_path` to `IrGenError` struct and `IrGenContext`, updated the single call site and print function.
- **Removed redundant headers**: Removed `"Semantic analysis failed:"` and `"IR generation failed."` prefix lines since the new format already starts with `error:`.
- **Converted `fprintf` import errors**: Changed three `fprintf(stderr, "Error: Cannot resolve import...")` calls to use `sem_error_list_add` for consistent formatted output.
- **Updated `unsupported_features.md`**: Moved `---` bodyless procedures to "Recently Added" section; added error reporting improvement.
- **All 73 tests pass**.

## Accomplishments (session 2026-07-03, continued)
- **Added error reporting for ~30 silent-failure sites in IR generation**: Added `ir_gen_error_collection_add` calls at ~30 locations across `ir_gen_lvalue`, `ir_gen_postfix_expression`, `ir_gen_node`, `ir_gen_binary_expression`, `ir_gen_unary_expression`, and `ir_gen_in_expression`. Covers P0 (silently-skipped operations on wrong types), P1 (symbol/type lookup failures including cast/transmute/len/make/new/incl/excl), and P2-P4 (tracked in `notes/ir_gen_error_reporting.md`).
- **Handled false-positive edge cases**: Package names in expression context return NULL from `ir_gen_identifier` without error (PostfixMember handler resolves via `op->resolved_symbol`). Blank identifier `_` returns NULL from `ir_gen_lvalue` without error. Outer PostfixExpression node does not add cascading error when inner `ir_gen_lvalue` already reported the cause.
- **All 73 tests pass**.
- **All 89 tests pass**.

## Accomplishments (session 2026-07-05, continued)

### Implemented `..any` variadic parameters
- **Grammar**: Added `VariadicMarker = DotDot @AST_ACTION_ELLIPSIS` wrapper rule. Updated `Parameter` rule: `Identifier Colon VariadicMarker? TypePrefix`.
- **Semantic analyser**: Detects `AST_NODE_ELLIPSIS` child inside `AST_NODE_PARAMETER` from `..` marker; resolves `..any` to `[]any` via `get_or_create_slice_type`.
- **LLVM function type fix**: `LLVMFunctionType` only gets `is_variadic = true` for bare `...` or C convention. `..any` uses a fixed `[]any` slice param, not LLVM varargs. Distinguishes by checking whether last param type is `TD_KIND_SLICE`.
- **Call-site packing**: Extra args packed into `[]any` slice (backing array of `any` structs, each packed via `ir_gen_pack_any`; slice struct built with `LLVMBuildInsertValue`). Distinguishes `..any` from bare `...` (no packing for bare `...`).
- **All 89 tests pass** (test_variadic_any.odin added).

## Summary
- **Maybe(T)** optional type with `.value`, `x.(T)`, `or_else`, `none` init.
- **`..any` variadic parameters**: grammar, semantic analysis (resolves to `[]any`), call-site packing into slice.
- **`type_of` runtime extraction**: `type_of(v: any)` now returns the runtime type_id from the any struct's field 1, enabling runtime type dispatch.
- **`core:fmt` package with `fmt.println`**: Works with both `int` and `string` arguments via `type_of`-based dispatch.
- **All 97 tests pass** (`test_variadic_type_assert` has pre-existing semantic return-type bug).

## Accomplishments (session 2026-07-06)

### Fixed `any` type system — 6 bugs
1. **Package-qualified calls missing arg evaluation**: The semantic analyser's package-qualified `POSTFIX_CALL` handler (for `pkg.func(args)`) never evaluated call arguments, leaving `resolved_type` NULL on all args. Added `sem_evaluate_expr` loop.
2. **Comma-chain right operands unevaluated**: `AST_NODE_EXPRESSION` handler in `sem_evaluate_expr` only recursed into `children[0]` (leftmost leaf), missing all right operands in `chainl1` comma chains. Changed to loop over all children.
3. **`inttoptr` in any packing**: `ir_gen_pack_any` used `inttoptr` for integer args, storing the integer value AS a pointer (address = value). Fixed: alloca+store for all non-pointer types. Same fix in `ir_gen_variable_decl` for non-`any`→`any` assignments.
4. **Type assertion stored pointer directly**: When `val` was a pointer (from subscript GEP), `LLVMBuildStore(val, tmp_alloca)` stored the pointer address into the `any` struct's data field instead of loading the value first. Fixed by adding a load-before-store check.
5. **`inttoptr→ptrtoint` in assertion**: Fixed the integer-extraction path from `ptrtoint` (converts data ADDRESS to int) to `bitcast+load` (loads the VALUE at the data address).
6. **`type_of` returned compile-time type**: `type_of(v)` where `v: any` returned the type_id of `any` itself, not the runtime type stored in the struct. Added runtime field extraction for `any` operands.

### Updated `stubs/core/fmt/fmt.odin`
- `println(args: ..any)` now handles both `int` (type_id 8) and `string` (type_id 45) arguments via `if type_of(v) == 8 { ... } else { ... }` dispatch.

### Test results
- 5 previously-failing tests now pass: `test_core_import`, `test_fmt_int`, `test_fmt_simple`, `test_variadic_assert`, `test_variadic_print`.
- Full suite: **97/98 pass** (only `test_variadic_type_assert` fails — pre-existing semantic return-type mismatch for type assertion in `-> bool` function).

## Accomplishments (session 2026-07-10)

### `@(private)`, `@(link_name)`, `@(require_results)` attributes
- **`@()` attribute grammar**: Added `AttrValue`, `AttrItem`, `AttrList`, `Attribute` grammar rules.
- **New AST nodes**: `AST_NODE_ATTRIBUTE`, `AST_NODE_ATTR_LIST`, `AST_NODE_ATTR_ITEM`.
- **`sem_analyse_attributes()`**: Extracts `link_name`, `require_results`, `is_private` into `ProcDeclAttributes` on `ConstantDecl` metadata.
- **`@(link_name="...")`**: IR gen uses attribute string for `LLVMAddFunction` name.
- **`@(require_results)`**: Parsed and stored (warning emission needs infra).
- **`@(private)`**: `sem_set_symbol_private` marks symbols; cross-package member access checks `sym->is_private`; `import_using_copy_symbol` skips private symbols in both `semantic_analyser.c` and `llvm_ir_generator.c`.
- **Tests**: `test_attribute.odin` (pass), `test_private_local.odin` (pass), `test_private_external.odin` (expected fail). All 105 tests pass.

### Recursive function call fix — fibonacci.odin
- **Link error `undefined reference to 'fib.4'`**: `ir_gen_top_level_decl` and `ir_gen_nested_procedure_decl` stored function value in symbol table AFTER body generation. Recursive calls inside body found `sym->value.value == NULL`, triggering spurious `LLVMAddFunction("fib", ...)` which created `fib.4` (LLVM uniquification). Fixed by moving `generator_add_symbol` before `ir_gen_node` in both functions.
- **Forward-declaration robustness**: Changed `LLVMAddFunction` to first try `LLVMGetNamedFunction` in `ir_gen_identifier`, preventing duplicate forward declarations.
- **Illegal instruction in `printf`**: `%d` handler type-asserted argument is `int`; `u64` argument caused `llvm.trap()`. Fixed by delegating `%d` and `%x` to `print_value` in stubs `fmt.odin`.

### Recursive call semantic fix — variable decl init
- **Root cause of `r: u64 = fib(v - 1) + fib(v - 2)` returning 0**: Top-level procedures registered with `type_info = NULL` during pass1; symbol table only updated with correct proc type AFTER body analysis. Recursive calls found `type_info = NULL`, causing `sem_evaluate_expr` to return NULL, so `resolved_type` was NULL. The IR generator's `init_node` check `child->resolved_type` failed → skipped init generation → variable stayed zero-filled.
- **Fix**: Added `proc_name` parameter to `sem_analyse_procedure_literal`. Symbol table is updated with the full procedure type BEFORE body analysis via `scope_add_symbol`.
- **`fibonacci.odin`**: Now uses clean code with zero casts — `return 1` and `return fib(v - 1) + fib(v - 2)` both work. Output: `fib(6) = 13`.

### Implicit conversion of untyped literals
- **`sem_can_implicitly_convert()`**: New function that checks whether an untyped integer/float literal can be implicitly converted to a target numeric type. Unwraps expression wrapper chains to find the underlying `AST_NODE_INTEGER_VALUE` or `AST_NODE_FLOAT_VALUE`. Used in `sem_analyse_return_statement` (both single and multi-return checks).
- **IR coercion for return values**: Added integer type coercion in `ir_gen_return_statement` — when the generated return value's LLVM type doesn't match the function's return type, a `LLVMBuildIntCast2` is inserted.
- **Tests**: `test_implicit_conv.odin` covers recursive fib with var-decl init, literal-to-u8/u16/u32/u64/i32/i64/f64, and u64+u64 addition.

### Centralized type coercion (`coerce_value_to_type`)
- **New function** `coerce_value_to_type(ctx, value, target_type, src_is_unsigned, name_hint)` handles all numeric type conversions in one place: integer↔integer (trunc/zext/sext), float↔float (fptrunc/fpext), integer↔float (sitofp/uitofp/fptosi/fptoui), pointer↔integer (ptrtoint/inttoptr).
- **Refactored `ir_gen_auto_cast_value`** to delegate to `coerce_value_to_type`, eliminating duplicate conversion logic.
- **Three implicit coercion sites refactored**: variable declaration initialization (`ir_gen_variable_decl`), return statement (`ir_gen_return_statement`), and binary expression RHS (`ir_gen_binary_expression`) — all now call `coerce_value_to_type` instead of inline `LLVMBuildIntCast2`/`LLVMBuildFPCast`.
- All 106 tests pass.

### `core:os` support — `os.exit()` and void-only main
- **`stubs/core/os/os.odin`**: New file — `exit` declared as `proc "c" (code: int) ---` via foreign libc.
- **Entry point wrapper updated**: `ir_generate()` always returns 0 from C `main()`. Exit code set via `os.exit()` calls (which terminate before the wrapper's `ret`).
- **Batch conversion script** (`scripts/convert_tests.py`): Mechanically transforms all test files from `main :: proc() -> int { return X }` to `main :: proc() { os.exit(X) }`. Handles inline `{ return X }` patterns, skips nested proc returns, skips `return` in comments.
- **Semantic error for non-void main**: `AST_NODE_CONSTANT_DECL` handler now checks if a `main` procedure has a non-void return type and emits a clear error.
- **`tests/test_param.odin` deleted**: Incompatible with void-only main; coverage provided by `test_proc_params.odin` and `test_call.odin`.
- **All 105 tests pass**.

### Extended `core:fmt` variants
- **Added `printfln`, `eprintln`, `eprintf`, `eprintfln`**: Each is a standalone inline copy of the format/print logic (no `..args` forwarding or `[]any` param delegation — both unsupported by the compiler). Text: `test_fmt_more.odin` covers all four.
- **Test runner**: `bash tests/run_tests.sh` confirms all 106 tests pass.
- **Root cause of delegation failure**: `ir_gen_identifier` returns alloca pointers for composite types (slices, structs, etc.) at line 291. When passed as function call arguments, the IR receives `ptr %args` instead of loading `{ptr, i64}`. Fix requires either loading composite type values at call sites or a separate argument evaluation path.

## Accomplishments (session 2026-07-14, continued)

### Implemented `@(builtin)` — Builtin annotation
- **`ProcDeclAttributes`** (`type_descriptors.h`): Added `bool is_builtin` field.
- **`sem_analyse_attributes()`** (`semantic_analyser.c`): Parses `@(builtin)` → sets `attrs->is_builtin = true`.
- **`ir_gen_top_level_decl()`** (`llvm_ir_generator.c`): Uses `is_builtin` as primary signal for intrinsic body generation. Known intrinsics dispatch via name-based `ir_gen_runtime_intrinsic_body()`. Unknown builtins emit compile-time error.
- **`stubs/core/runtime/runtime.odin`**: All 8 runtime intrinsic declarations now use `@(builtin)`.
- **Tests**: `test_builtin.odin` uses `@(builtin)` with `print_string`/`int_to_string`/`os_exit`. All 124 tests pass.
- **Root cause**: The `delimited` combinator in strict mode (`delimited_flex` available at `easy_pc/lib/parsers.c:2875`) errors on trailing separators instead of backtracking over them. The `Semicolon?`/`Comma?` after every `delimited` in the grammar was dead code — never reached.
- **Fix**: Changed all 8 `delimited(X, Sep)` calls in `odin_grammar.gdl` to `delimited_flex(X, Sep)`. `delimited_flex` backtracks over the trailing separator, then `Sep?` consumes it. This fixes `:: struct { x: int; }`, `enum { A; B; }`, `union { x: int; y: int; }`, `@(a, b,)`, `bit_field { a: 0; b: 1; }`, etc.
- **Tests**: `test_type_alias_struct.odin` uses `:: struct` with field access. All 123 tests pass.

### Implemented `#align` struct/field alignment
- **Grammar** (`odin_grammar.gdl`): Added `KwAlign` lexeme, added to `DirectiveName`. `StructType` changed to `KwStruct (SoaType | ((Directive IntegerLiteral?)? StructRawBody))`. `StructField` changed to accept `(Directive IntegerLiteral)?` after `TypePrefix`.
- **Semantic analyser** (`semantic_analyser.c`): `AST_NODE_STRUCT_TYPE` handler scans children for `AST_NODE_DIRECTIVE` with text `"#align"` and extracts subsequent `AST_NODE_INTEGER_VALUE`. Overrides `struct_metadata.alignment` after `register_struct_type`. Field-level `#align` extracted and stored in `struct_field_t.user_alignment`.
- **IR generator** (`llvm_ir_generator.c`): `ir_gen_variable_decl` uses `struct_metadata.alignment` for `LLVMSetAlignment` when it exceeds ABI alignment.
- **Pre-existing bug discovered**: `:: struct { ... }` (ConstantDecl with inline struct type) never worked even before changes — `End of input not found` error.
- **Tests**: `test_align.odin` with struct-level and field-level `#align` in variable declarations.
- **All 122 tests pass** (121 previous + 1 new).

### Key insight
The `any` type system had two fundamental flaws: (a) integer arguments were stored as `inttoptr` values (data pointer = integer cast to pointer) instead of storing the integer in memory and pointing to it; (b) the `type_of` builtin only worked at compile time, making runtime type dispatch impossible. Fixing both enabled proper runtime type identification and safe type assertion through the `any` struct's `{ptr data, i64 type_id}` layout.

## Accomplishments (session 2026-07-14)

### Removed name-based intrinsic detection (cleanup)
- **Removed `ir_gen_is_runtime_intrinsic()`**: The name-based detection function was redundant now that all runtime intrinsics use `@(builtin)`. The IR generator's `ir_gen_top_level_decl()` now uses `attrs->is_builtin` as the sole signal for intrinsic body generation.
- **Simplified dispatch**: `body_node || is_builtin` replaces the previous `body_node || is_builtin || is_known_intrinsic` condition. The name-based dispatch inside `ir_gen_runtime_intrinsic_body()` is now only reached from the `@(builtin)` path.
- **Safety**: Added a final `else` clause to `ir_gen_runtime_intrinsic_body()` that emits `LLVMBuildUnreachable` for unknown builtin names (previously fell through with no terminator, causing LLVM verification errors).
- **All 124 tests pass**.

### Implemented runtime bounds checking on array/slice/string subscripts
- **`IrGenContext`** (`llvm_ir_generator.h`): Added `bool bounds_checking_enabled` field, initialized to `true` in `ir_gen_context_create`.
- **`ir_gen_emit_bounds_check()`** (`llvm_ir_generator.c`): Helper that emits `icmp uge index, len` → conditional branch to trap block. Trap block calls `llvm.trap()` + `unreachable`. Continuation block resumes normal flow. Returns the (possibly extended) index value.
- **Forward declaration**: Added `static LLVMValueRef ir_gen_emit_bounds_check(...)` to the forward declarations section to resolve implicit declaration conflict (function used in `ir_gen_lvalue` at line ~1666, defined at line ~3971).
- **Lvalue subscript path** (`ir_gen_lvalue`): For `TD_KIND_ARRAY`, uses `cur_type->as.array.count` as compile-time length constant. For `TD_KIND_SLICE`/`TD_KIND_DYNAMIC_ARRAY`, loads `.len` field (field index 1) via GEP. For `string`, loads `.len` field via GEP. Bounds check emitted before the element-access GEP.
- **Rvalue subscript path** (`ir_gen_postfix_expression`): Same logic. For arrays, uses compile-time count. For slices/dynamic arrays, loads `.len` via GEP. For strings, uses `LLVMBuildExtractValue` on the string struct to get `.len` (field index 1).
- **`#no_bounds_check` directive**: `AST_NODE_DIRECTIVE` handler in `ir_gen_node` now checks for `#no_bounds_check` and sets `ctx->bounds_checking_enabled = false`. This is a simple toggle (once disabled, stays disabled for the rest of the compilation unit).
- **Map subscripts**: Not bounds-checked (use linear-scan lookup, not direct indexing).
- **Tests**: `test_bounds_check.odin` (7 subtests: array read/write, slice read/write, string read — all in-bounds, verify correctness). Updated `test_no_bounds_check.odin` to test OOB array access with `#no_bounds_check` directive (program runs without trapping). Manually verified: OOB access without `#no_bounds_check` produces SIGILL (exit 132) from `llvm.trap()`.
- **All 125 tests pass** (124 previous + 1 new; `test_no_bounds_check.odin` updated from no-op syntax test to functional test).

### Key insight
The `if condition do statement` form is not supported by the grammar (only `if condition { statements }` with braces). This was a pre-existing limitation never exposed by tests. The `arr[:]` full-slice syntax is also unsupported — only `arr[..]` works.

## Accomplishments (session 2026-07-14, continued)

### Implemented `if cond do stmt` grammar form
- **Grammar** (`odin_grammar.gdl`): Added `KwDo = lexeme("do" IdBoundary)` keyword. Added `KwDo` to `AllReservedWords` (prevents `do` from being used as an identifier). Changed `IfStatement` rule from `CompoundStatement` to `(CompoundStatement | (KwDo Statement))` for both then and else branches, allowing `if cond do stmt`, `else do stmt`, `else if cond do stmt` alongside the existing braced form.
- **Semantic analyser** (`semantic_analyser.c`): The `AST_NODE_IF_STATEMENT` handler previously only handled `AST_NODE_COMPOUND_STATEMENT` children for scope/analysis; other children fell through to `sem_evaluate_expr`, which broke for statement nodes (`os.exit()`, etc.). Added explicit dispatch for statement-type children (`AST_NODE_EXPRESSION_STATEMENT`, `AST_NODE_ASSIGN_STATEMENT`, `AST_NODE_VARIABLE_DECL`, `AST_NODE_RETURN_STATEMENT`, etc.) to `sem_pass2_node` with scope push/pop.
- **IR generator**: No changes needed — `ir_gen_if_statement` already uses `ir_gen_node` for branch bodies, which dispatches correctly for both `CompoundStatement` and `ExpressionStatement`.
- **Tests**: `test_if_do.odin` (basic if-do), `test_if_do_else.odin` (if-do + else-if-do + else-do + mixed braces).
- **All 128 tests pass** (125 previous + 3 new).

### Implemented `arr[:]` full-slice syntax
- **Grammar** (`odin_grammar.gdl`): Added `PostfixOpFullSlice = LBracket Colon RBracket @AST_ACTION_POSTFIX_SLICE` rule. Added `PostfixOpFullSlice` to `PostfixOp` alternatives. Reuses the same AST action as `PostfixOpSlice`, so `arr[:]` produces an `AST_NODE_POSTFIX_SLICE` node identical to `arr[..]`.
- **No semantic/IR changes needed**: The existing `POSTFIX_SLICE` handling in semantic analyser and IR generator already treats both `arr[..]` and `arr[:]` identically (full slice with `low=0, high=array.count`).
- **Tests**: `test_arr_full_slice.odin` (3 subtests covering `arr[:]` with explicit type, `:=` shorthand, element access).
- **All 128 tests pass** (no regressions).

### Key insight (this session)
The `@AST_ACTION_POSTFIX_SLICE` action is shared between `PostfixOpSlice` (for `arr[..]`, `arr[low..high]`, etc.) and the new `PostfixOpFullSlice` (for `arr[:]`). Both produce `AST_NODE_POSTFIX_SLICE` nodes, so the semantic analyser and IR generator handle them identically without needing to distinguish the source syntax.

## Accomplishments (session 2026-07-23)

### Verified polymorphic forward declarations (no code changes needed)
- **Finding**: Forward declarations of poly procs (`foo :: proc($T: typeid, x: T) -> T ---`) followed by definitions (`foo :: proc($T: typeid, x: T) -> T { ... }`) **already work** without any code changes.
- **Root cause**: `poly_register_origin` at `polymorphism.c:333` already overwrites the origin when the same symbol is registered twice. Since forward declarations must come before definitions in Odin, the definition (with body) is always the last one processed. `poly_get_origin` returns the latest ConstantDecl, which contains the `ProcedureDefinition` with the `CompoundStatement` body.
- **Verified**: basic `identity`, `add`, `swap_values`, where-clause `identity_int_only` — all work with forward declarations. Mixed poly + non-poly forward declarations work.
- **Tests**: `test_poly_forward_decl.odin` added with 7 subtests (int identity, float identity, int add, int swap, float swap, where-clause int identity).
- **All 171 tests pass** (170 previous + 1 new).

### Implemented cross-package polymorphic procs (Stage 11)
- **Root cause #1 (package-qualified poly calls)**: The package-qualified branch of `sem_evaluate_postfix_expr` at `sem_evaluate_expr.c:1198` had no `is_polymorphic` arm — only `TD_KIND_PROC` (non-poly) and `TD_KIND_OVERLOAD_BUNDLE`. A package-qualified poly call like `pkg.identity(42)` fell through with the unspecialized proc type, silently leaving the result variable undeclared. **Fix**: Before the `TD_KIND_PROC` check, retrieve the symbol from the preceding `POSTFIX_MEMBER` op's `resolved_symbol` (`postfix_ops->list.children[i-1]`). If `sym->is_polymorphic`, evaluate args via `sem_collect_comma_chain_args` + `sem_evaluate_expr`, call `poly_resolve_call`, and populate `call_op->resolved_symbol` / `call_op->resolved_type` from the specialization (mirrors the local poly branch at lines 1310–1376).
- **Root cause #2 (`import using` poly copies)**: `import_using_copy_symbol` (both in `semantic_analyser.c:930` and duplicate in `llvm_ir_generator.c:2825`) only propagated `name`, `value`, `const_int_val`, and `has_const_int_val` — NOT `is_polymorphic`. The new local copy was also never registered with `poly_register_origin`, so `poly_get_origin(copy)` returned NULL. **Fix**: Both copies now detect `sym->is_polymorphic`, propagate the flag to the copy, and call `poly_register_origin(copy, poly_get_origin(sym))` to register the origin AST.
- **IR generation**: No changes needed — `ir_gen_postfix_call` Priority 1 already handles specializations via `op->resolved_symbol` and forward-declares the mangled function via `LLVMGetNamedFunction` / `LLVMAddFunction`.
- **Test helper package** at `tests/test_poly_cross_pkg_helper/test_poly_cross_pkg_helper.odin`: `identity` (no where), `identity_int_only` (typeid where), `sum_same_size` (size_of where), `add` (two poly params), `identity_int` (non-poly for bundle), `mixed_bundle` (poly + non-poly bundle), `helper_int` (baseline).
- **Tests**: `test_poly_cross_pkg.odin` (package-qualified poly calls — int, float, where-clause, two-param, size_of where, non-poly baseline), `test_poly_cross_pkg_using.odin` (`import using` unqualified poly calls), `test_poly_cross_pkg_bundle.odin` (cross-package overload bundle dispatch), `expected_to_fail/test_poly_cross_pkg_where_mismatch.odin` (cross-package where-clause mismatch correctly produces compile error).
- **All 175 tests pass** (171 previous + 3 new regular + 1 new expected-to-fail).

## Accomplishments (session 2026-07-14, continued)

### Implemented exhaustiveness checking for enum switches
- **Type descriptors** (`type_descriptors.h`): Extended `enum_type` struct in `TD_KIND_ENUM` union with `char const ** enumerator_names`, `long long * enumerator_values`, and `int enumerator_count` fields. `calloc` zero-initializes these in `type_descriptor_alloc`, so no changes needed in `get_or_create_enum_type` for new field initialization.
- **Semantic analyser** (`semantic_analyser.c`): `AST_NODE_ENUM_TYPE` handler now counts enumerators, allocates arrays via `calloc`, and stores each enumerator's name and value in the type descriptor. The allocation is guarded against re-running on deduplicated enum types (only allocates when `enumerator_count == 0`).
- **`AST_NODE_SWITCH_STATEMENT` handler** (`semantic_analyser.c`): Now detects `#partial` directive among switch children, tracks whether the switch has a `default` case, evaluates the switch expression to determine its type, collects covered case values for enum-typed switches, and emits `switch is not exhaustive: missing case for enum value '<name>'` errors for each uncovered enumerator. The check is suppressed by `#partial` directive or by the presence of a `default` case.
- **`#partial` directive syntax**: `#partial` sits after the `switch` keyword (per grammar `SwitchStatement = KwSwitch Directive? Expression? SwitchBody`), so the correct syntax is `switch #partial c { ... }`, not `#partial switch c { ... }`.
- **Tests**: `test_switch_enum_exhaustive.odin` (full coverage — passes), `test_switch_enum_partial.odin` (partial coverage with `#partial` — passes), `test_switch_enum_default.odin` (partial coverage with default case — passes), `expected_to_fail/test_switch_enum_missing.odin` (missing case without `#partial` or default — expected to fail because exhaustiveness error).
- **All 132 tests pass** (128 previous + 3 new regular + 1 new expected-to-fail).
- **Pre-existing limitation discovered**: `Color.Green` qualified enumerator access does not work (errors "type has no member"). Only bare enumerators (`Green`) work. This is a separate limitation tracked implicitly.
