# Refactoring Plan

Based on comprehensive analysis of the codebase (July 2026). The compiler has grown organically from ~19K lines across 44 source files. This plan identifies the most impactful refactoring opportunities, ordered by expected benefit-to-effort ratio.

---

## Phase 1: Extraction of Utility Functions (Low Effort, High Payoff)

These extractions eliminate repeated code patterns without changing architecture.

### 1.1 Expression chain unwrapping helper ✅ DONE (already exists in ast_utils.c:54)

The pattern `while (node->list.count >= 1 && node->list.children[0]) node = node->list.children[0]` (or similar) appears **7+ times** across the codebase:

- `ir_gen_lvalue` default case (line ~2227)
- `ir_gen_bit_set_assign_expr` (line ~2682)
- `ir_gen_assign_expression` (line ~3007)
- `ir_gen_assign_statement` (line ~3104)
- `ir_gen_evaluate_constant_bool` (line ~7342)
- `ir_gen_evaluate_constant_int` (line ~7306)
- `ir_gen_node_contains_auto_cast` (line ~3190)

**Action**: Create `expression_chain_unwrap(odin_grammar_node_t * node)` in `ast_utils.c`. Replace all 7+ instances.

### 1.2 `is_type_node` → lookup table ✅ DONE (already using lookup table)

`is_type_node()` in `ast_utils.c` (21-case switch) must be updated every time a new type node is added. The AST enum currently has ~155 values, ~22 of which are type nodes.

**Action**: Replace the switch with a static boolean lookup table indexed by `odin_grammar_node_type_t`, initialized at startup or with C99 designated initializers. Alternatively, add a `NODE_CATEGORY_TYPE` flag to the enum definition metadata.

### 1.3 Escape sequence processing helper ✅ DONE (already extracted)

`ir_gen_string_literal` (line ~363, ~183 lines) duplicates escape sequence parsing between its count-pass and fill-pass. The `\xNN` hex parsing (~25 lines) is duplicated.

**Action**: Extract `process_escape_sequence(const char **p, unsigned char *out_byte)` helper. Returns the number of consumed input bytes and the decoded byte value. Use in both passes. **Also applied to `ir_gen_rune_literal`** which had its own inline duplicate.

### 1.4 Cleanup path in `main()` ✅ DONE

`main()` has 4 near-identical cleanup paths (lines ~303, ~330, ~508, ~568) each frees `type_registry`, `sem_ctx`, `ir_ctx`, `gen_ctx`.

**Action**: Factor into a single `goto cleanup` pattern at end of `main()`, or extract `cleanup_resources()` function.

---

## Phase 2: Eliminate Duplicate Switch Cases (Medium Effort)

These changes merge repeated case handlers within a file.

### 2.1 String/slice/dynamic_array member access — unify 3 copies ✅ DONE

The string `.len`/`.data`, slice `.len`/`.data`, dynamic_array `.len`/`.cap`/`.data`, array `.len` access pattern appears in **3 locations**:
- `ir_gen_lvalue` POSTFIX_MEMBER (lines ~2061–2162)
- `ir_gen_postfix_expression` POSTFIX_MEMBER (lines ~5505–5625)
- `ir_gen_postfix_expression` POSTFIX_SUBSCRIPT (lines ~5142–5239, for len extraction in bounds check)

Each handles the same 4 type kinds (`TD_KIND_BASIC "string"`, `TD_KIND_SLICE`, `TD_KIND_DYNAMIC_ARRAY`, `TD_KIND_ARRAY`) with GEP-based field extraction.

**Action**: Extract `ir_gen_load_field_of_aggregate(ctx, LLVMValueRef val, TypeDescriptor *type, const char *field_name, bool is_lvalue)` that returns a pointer or value for the named field. All three call sites use this single helper.

### 2.2 Map linear-scan subscript — unify 2 copies ✅ DONE

The map linear-scan loop (load entry → check key → found/not-found/next → merge) in:
- `ir_gen_lvalue` POSTFIX_SUBSCRIPT (lines ~1783–1961, ~178 lines)
- `ir_gen_postfix_expression` POSTFIX_SUBSCRIPT (lines ~5261–5392, ~131 lines)

These differ only in SSA naming prefixes and one being lvalue vs rvalue.

**Action**: Extract `ir_gen_map_subscript(ctx, map_val, map_type, index_val, out_val_type, bool is_lvalue, const char *prefix)`. Returns a pointer for lvalue or a phi-selected value for rvalue. Implemented with an `ir_gen_map_append_block()` helper for block creation.

### 2.3 `ir_gen_assign_expression` / `ir_gen_assign_statement` — unify 2 copies ✅ DONE

These functions (~60 lines each) are ~90% identical. Both check: vector → bit_field → bit_set → lvalue_ptr → any packing → compound assign → store. The only difference is child-count indexing (`< 3` vs `< 2`).

**Action**: Extract `ir_gen_assign_internal(ctx, node, bool is_statement)` and have both functions delegate to it.

### 2.4 Pointer auto-dereference — unify 3 variants

Pointer auto-deref before member access appears in:
- `ir_gen_lvalue` POSTFIX_MEMBER (line ~1994): loads pointer, falls through
- `ir_gen_postfix_expression` POSTFIX_MEMBER (line ~5496): just updates type
- `ir_gen_postfix_expression` POSTFIX_DEREF (line ~5738): explicit deref

**Action**: Document as intentional (the different semantics are correct), but add a shared helper `ir_gen_auto_deref_pointer(ctx, val, type)` that returns `(new_val, new_type)` so at least the type-checking logic is shared.

### 2.5 `DEFINE_ACTION` / `DEFINE_TERMINAL_ACTION` — unify ✅ DONE

These two macros in `odin_grammar_ast_actions.c` differ only by the `capture_text` boolean passed to `make_node()`.

**Action**: Merge into a single `DEFINE_ACTION(name, type, capture_text)` with `DEFINE_TERMINAL_ACTION` as a convenience wrapper.

### 2.6 `sem_error.c` / `ir_gen_error.c` — unify ✅ DONE

These are **structurally identical** files (57 lines each, same struct layout, same function signatures, same `print_location` helper). The only differences: prefix names (`SemError` vs `IrGenError`, `sem_error_` vs `ir_gen_error_`).

**Action**: Create a single `error_list.<c|h>` with `ErrorList` type parametrized by max count. Or use a macro-generated generic. `SemErrorList` and `IrGenErrorCollection` become typedefs.

---

## Phase 3: Split Monster Functions (High Effort, High Payoff)

These are the largest functions in the codebase. Each should be split into focused sub-functions.

### 3.1 ✅ `sem_evaluate_expr` — 1555 lines (line 1707)

Contains 43+ case labels handling everything from integer literals to postfix expressions. The POSTFIX_EXPRESSION subtree alone (~600 lines, two copies) handles call, member, subscript, slice, deref, assertion.

**Done**: Step 1 — each case label group extracted into a named `sem_evaluate_<type>_expr()` function. The switch statement is now ~45 lines of pure dispatch. 42 new functions defined after `sem_evaluate_expr`.

**Next**: Phase 3.2 (split `sem_resolve_type_expr`).

**Action**:
- Step 1 ✅ Each case body extracted to `sem_evaluate_<type>_expr()` function  
- Step 2 ✅ Replace switch with `AST_NODE_COUNT`-sized function pointer lookup table

**Target**: `sem_evaluate_expr` reduced from 1555 to 8 lines of dispatch logic + 42 extracted functions.

### 3.2 ✅ `sem_resolve_type_expr` — 1272 lines (line 435)

19 type cases (BASIC/TYPE_NAME combined, plus `AST_NODE_IDENTIFIER` as a separate case).

**Done**: Step 1 — each case body extracted into a named `sem_resolve_<type>_type()` function (19 case groups → 18 extracted functions + 1 delegated to existing `sem_resolve_procedure_signature`). Step 2 — switch replaced with `AST_NODE_COUNT`-sized function pointer lookup table `sem_resolve_type_dispatch[]`.

**Action**:
- Step 1 ✅ Each case body extracted to `sem_resolve_<type>_type()` function
- Step 2 ✅ Replace switch with `AST_NODE_COUNT`-sized function pointer lookup table

**Target**: `sem_resolve_type_expr` reduced from 1272 to 10 lines of dispatch logic. **Achieved**.

### 3.3 ✅ `ir_gen_postfix_expression` — 1236 lines (line 4860) — DONE

6 case labels (call, subscript, member, deref, assertion, slice) each extracted into named helpers:
- `ir_gen_postfix_call` (returns `bool` for fatal-error early-exit on "called value is not a procedure")
- `ir_gen_postfix_subscript`
- `ir_gen_postfix_member`
- `ir_gen_postfix_deref`
- `ir_gen_postfix_assertion`
- `ir_gen_postfix_slice` (handles both POSTFIX_SLICE and POSTFIX_SLICE_LT)

Each helper takes `(ctx, op, &val, &cur_type)` and updates state via pointers.
Main function reduced to ~50 lines of dispatch logic. Net -16 lines.
All 155 tests pass.

### 3.4 ✅ `ir_gen_node` — 1069 lines (line 6232) — DONE

11 largest inline cases extracted as named functions (functions were already extracted, case bodies replaced with 1-line calls): `ir_gen_cast_expr`, `ir_gen_len_cap_expr`, `ir_gen_offset_of_expr`, `ir_gen_raw_data_expr`, `ir_gen_make_expr`, `ir_gen_delete_expr`, `ir_gen_incl_excl_expr`, `ir_gen_compress_values_expr`, `ir_gen_soa_zip_expr`, `ir_gen_soa_unzip_expr`, `ir_gen_directive`.

All 155 tests pass.

### 3.5 `ir_gen_lvalue` — 649 lines (line 1588)

Handles identifier, context, and postfix-expression lvalue resolution. The inner POSTFIX_SUBSCRIPT case (~400 lines) and POSTFIX_MEMBER case (~230 lines) are the bulk.

**Action**: Extract `ir_gen_lvalue_postfix_subscript()` and `ir_gen_lvalue_postfix_member()` helpers.

---

## Phase 4: Modularization (Incremental)

### 4.1 Split `semantic_analyser.c` into domain files

Currently 5434 lines with 4 concerns mixed:
- Expression evaluation (`sem_evaluate_expr`)
- Type resolution (`sem_resolve_type_expr`)
- Pass 1 (registration) and Pass 2 (body analysis)
- Statement analysis

**Action**:
- `sem_type_resolver.c` — `sem_resolve_type_expr` and helpers
- `sem_expr_evaluator.c` — `sem_evaluate_expr` and helpers
- `sem_statement_analyser.c` — `sem_pass2_node`, return/if/for/switch/compound analysis
- `sem_declaration.c` — pass1 registration, pass2 body analysis for top-level
- `sem_context.c` — `SemContext` create/destroy/import helpers

### 4.2 Split `llvm_ir_generator.c` into domain files

Currently 7791 lines. Could be split similarly:
- `ir_expression.c` — expression-level IR generation (binary, unary, logical, tertiary, etc.)
- `ir_lvalue.c` — lvalue resolution and assignment
- `ir_statement.c` — statement IR generation (if, for, switch, return, defer, etc.)
- `ir_postfix.c` — postfix expression generation (call, subscript, member, slice, deref, assertion)
- `ir_intrinsic.c` — runtime intrinsic body generation
- `ir_context.c` — context creation/destruction, and top-level orchestration

### 4.3 Split `type_descriptors.c` into domain files

Currently 1737 lines. The `get_or_create_*` family has ~25 functions:
- `type_kind.c` — basic, pointer, array, slice, multi-pointer, dynamic-array
- `type_compound.c` — struct, union, soa, enum
- `type_functional.c` — proc, overload-bundle
- `type_nominal.c` — distinct, maybe, bit_set, bit_field, map, range, vector, tuple
- `type_canonical.c` — canonical name writing, hash computation

### 4.4 `main()` → separate CLI / pipeline modules

Currently 583 lines with CLI parsing, compiler pipeline, and linker invocation all in `main()`.

**Action**: Extract:
- `cli.c`/`cli.h` — argument parsing, help text, `run_linker()`
- `compiler.c`/`compiler.h` — `compile_package()` function that orchestrates the pipeline

---

## Phase 5: Architecture Improvements (Ongoing)

### 5.1 Table-driven action registration

Replace 112 hand-written `REGISTER()` calls in `odin_grammar_ast_actions.c` (lines 462–622) with a table:

```c
static action_registration_t const action_table[] = {
    {AST_ACTION_PROGRAM, ast_action_program_action},
    {AST_ACTION_PACKAGE_CLAUSE, ast_action_package_clause_action},
    // ...
};
```

Add a compile-time static assertion that the table size matches the action enum count.

### 5.2 Structured parser/IR gen error type

`sem_error.c` and `ir_gen_error.c` are near-identical. After unifying (Phase 2.6), consider adding error severity (warning/error) and error codes for better testability.

### 5.3 Import path resolution helper ✅ DONE

In `package_resolver.c`, the 7 copy-paste `malloc → snprintf → file_exists → free` blocks for path resolution should be replaced with a single `try_resolve_path(base_dir, const char *fmt, ...)` that returns an allocated path or NULL.

### 5.4 Static analysis / lint

Add `const` correctness pass — many functions accept mutable pointers they don't modify. Add `-Wmissing-prototypes` to catch missing includes.

### 5.5 Unify `ParsedFile` and `ImportedPackage`

In `package_resolver.c`, these two structs have nearly identical fields. Consider a single `SourceFile` type with an `is_imported` flag.

### 6.0 Get rid of all compiler warnings

Unreferenced enums in switch statements. Use #pragma to disable warnings, or handle each case, perhaps with an error if the enum value shouldn't occur in the switch
There is at least one case where a buffer passed to snprintf() may be too small.

---

## Execution Strategy

| Phase | Effort | Risk | Files Affected | Suggested Order |
|-------|--------|------|----------------|-----------------|
| 1.1 Expression unwrap ✅ DONE | Low | Low | ast_utils.c + 7 call sites | 1st |
| 1.2 is_type_node table ✅ DONE | Low | Low | ast_utils.c | 2nd |
| 1.3 Escape helper ✅ DONE | Low | Low | llvm_ir_generator.c | 3rd |
| 1.4 Cleanup goto ✅ DONE | Low | Low | main.c | 4th |
| 2.1 Aggregate field access ✅ DONE | Medium | Medium | llvm_ir_generator.c | 5th |
| 2.2 Map subscript ✅ DONE | Medium | Medium | llvm_ir_generator.c | 6th |
| 2.3 Assign unify ✅ DONE | Medium | Low | llvm_ir_generator.c | 7th |
| 2.5 Action macro unify ✅ DONE | Low | Low | odin_grammar_ast_actions.c | 8th |
| 2.6 Error list unify ✅ DONE | Low | Low | sem_error.c, ir_gen_error.c | 9th |
| 5.3 Import path helper ✅ DONE | Low | Low | package_resolver.c | 10th |
| 3.1 Split sem_evaluate_expr ✅ DONE | High | High | semantic_analyser.c → multiple | 11th |
| 3.2 Split sem_resolve_type_expr ✅ DONE | High | High | semantic_analyser.c → multiple | 12th |
| 3.3 Split ir_gen_postfix_expression ✅ DONE | Medium | Medium | llvm_ir_generator.c → multiple | 13th |
| 3.4 Split ir_gen_node ✅ DONE | Medium | Low | llvm_ir_generator.c | 14th |
| 3.5 Split ir_gen_lvalue | Medium | Medium | llvm_ir_generator.c | 15th |
| 4.1–4.4 Split files | High | Medium | Multiple files | After function splits |
| 5.1 Table-driven actions | Medium | Medium | odin_grammar_ast_actions.c | 16th |
| 5.4 Const correctness | Low | Low | All files | Ongoing |

**Can be done independently at any time** (no dependency on other phases):
- 1.4 Cleanup goto
- 2.5 Action macro unify
- 2.6 Error list unify
- 5.3 Import path resolution helper
- 5.4 Const correctness

**Requires careful testing** (regression risk):
- 2.1–2.3 Any merged switch cases
- 3.1–3.5 Any function splits
- 4.1–4.4 Any file splits

### Verification Strategy

After each refactoring step:
1. `cmake --build build` — must compile without warnings
2. `bash tests/run_tests.sh` — all tests must pass (currently 155)
3. Manually run a few edge-case tests that exercise the refactored path
4. For function extractions: verify the extracted function is called from the right places
