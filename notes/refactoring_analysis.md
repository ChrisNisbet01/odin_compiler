# Refactoring Analysis Report

Comprehensive analysis of the C compiler source (2026-08-06). Companion to
`refactoring_plan.md` (which tracks the older Phase 1–5 work). This file
tracks the current analysis + const-correctness sweep.

---

## Part 1 — Why TypeDescriptors have their constness cast away

**Root cause:** the AST node struct declares the semantic-analysis annotation
field as non-const at `src/odin_grammar_ast.h:215`:

```c
TypeDescriptor * resolved_type;   // non-const
```

The type *registry* (`get_or_create_*_type`) returns non-const
`TypeDescriptor *`, so storing those into `resolved_type` needs no cast. But
semantic analysis also stores **already-resolved descriptors it received as
`TypeDescriptor const *`** (from function signatures, registry lookups, etc.)
onto the node — that forces `(TypeDescriptor *)` at ~180 sites:

- `sem_evaluate_expr.c`: 120 (nearly all pure stores to `resolved_type`)
- `sem_type_resolver.c`: 36 (34 stores + 2 genuine mutations)
- `semantic_analyser.c`: 19 (18 stores + 1 genuine mutation)
- `type_descriptors.c`: 5 (registry-internal, harmless)

**Genuine mutations** (lazily-populated caches stored *inside* the shared
descriptor — these must keep an explicit, commented cast):

1. `sem_type_resolver.c:812` — `(TypeDescriptor *)enum_td` to populate
   `enumerator_names` / `enumerator_values` after `get_or_create_enum_type`
   dedup (guarded by `enumerator_count == 0`).
2. `semantic_analyser.c:879` — `(TypeDescriptor *)proc_type` to fill
   `proc_metadata.default_values[]` (AST node pointers for default params).
3. One more in `sem_type_resolver.c` (struct alignment override or similar) —
   catalogue exactly during implementation.

**Key fact:** only **two** non-const `TypeDescriptor *` declarations exist in
headers: `resolved_type` (`odin_grammar_ast.h:215`) and
`type_compute_hash(TypeDescriptor *)` (`type_descriptors.h:341`). Almost every
function already takes `TypeDescriptor const *`, so the ripple of making the
field const is small and compiler-guided.

**Fix chosen:** change `resolved_type` to `TypeDescriptor const *`, make
`type_compute_hash` take const (pure read — verify no lazy mutation), then let
the compiler list every consumer that needs const-ifying (each is a read-only
pass). The ~3 genuine mutation sites keep an explicit commented cast. Optional
later: hoist enum-enumerator and default-values caches into a side table in
`SemContext` keyed by descriptor pointer. Net: removes ~170 casts.

---

## Part 2 — Refactoring opportunities by category

### A. Duplicated composite-type kind lists (quick, low risk)
The 11-kind "don't auto-load, return the pointer" check is copy-pasted:
- `llvm_ir_generator.c:312-318` (identifier loading)
- `ir_gen_postfix.c:1823` (rvalue auto-load)
- likely `ir_gen_assign.c` too

Extract `static inline bool td_is_composite(TD_KIND)` in `type_descriptors.h`,
replace all sites.

### B. Duplicated expression-wrapper walk loops
- `sem_check.c:19-41` and `sem_check.c:71-82` each walk `children[0]` through
  the same ~15 wrapper node types
- `ir_gen_assign.c` has its own `is_expression_wrapper_type` + 4 walkers
  (incl. `ir_gen_postfix.c:1766`)

Extract one shared `sem_unwrap_expression_chain(node)` + single
`is_expression_wrapper_type` (reuse the one already in `ir_gen_assign.c`).

### C. Name-based dispatch chains
- `polymorphism.c:341-417` — 40-entry `strcmp(name, "type_is_*")` chain plus
  the `type_elem_type` / `type_base_type` / `type_core_type` section. Convert
  to a static `{name, bool (*)(TypeDescriptor const *)}` lookup table + one
  scan/bsearch. Highest-value dispatch conversion.
- `ir_gen_postfix.c:463-525` — 4 matrix-intrinsic branches, each with
  different arg-count and result-type logic. Extract each branch to a named
  `ir_gen_postfix_*_call()` helper (dispatch chain itself fine at 4 entries).
  Note: `intrinsic_handlers` in `ir_intrinsic.c:541-544` is already a
  hash-table dispatch — the established pattern to copy.

### D. Hard-to-fathom conditions → named booleans
- Composite-kind `a||b||c…` chains (A above) become
  `bool is_composite = td_is_composite(kind);`
- `polymorphism.c:988` `|| strcmp(...)` conditions → `is_type_predicate(name)`

### E. Large functions / case bloat (continue established extraction pattern)
- `sem_evaluate_postfix_expr` — 938 lines (`sem_evaluate_expr.c:2058-2995`)
- `sem_pass2_node` — 907 lines (`semantic_analyser.c:2203-3109`)
- `sem_pass1_register_top_level_ex` — 585 lines (`semantic_analyser.c:1608-2192`)
- `ir_gen_node` — 462 lines (`llvm_ir_generator.c:2870-3331`), remaining inline cases
- `sem_evaluate_constant_int` (343), `sem_resolve_procedure_signature` (339,
  `semantic_analyser.c:572-910`), `ir_generate` (300,
  `llvm_ir_generator.c:3992-4291`)

### F. Flag-setting list-search loops
Scan-for-a-node loops that set a flag/index (find type node among children,
`#partial` directive, `default` case, where-clause, expand_values arg) →
extract predicates like `contains_directive(children, count, "#partial")`,
`find_field_type_node(...)`.

---

## Status tracker

- [x] Const-correctness sweep (Part 1): `resolved_type` made `TypeDescriptor const *`
      in `odin_grammar_ast.h`; removed all 171 redundant `resolved_type = (TypeDescriptor *)`
      write-casts (sem_evaluate_expr.c ×120, sem_type_resolver.c ×34, semantic_analyser.c ×17),
      removed the redundant `sym->value.type_info = (TypeDescriptor *)resolved` cast
      (semantic_analyser.c, both sides already const), and the pointless
      const→non-const→const round-trip in `get_or_create_arena_type` (type_descriptors.c:1661).
      Remaining casts are exactly 3 documented mutation sites (enum enumerator cache,
      struct `#align` override, proc default-values cache) + 4 legitimate registry
      storage casts. `type_compute_hash` kept non-const (it lazily caches `type_id`;
      its callers already hold non-const pointers — it contributed no casts).
      **246/246 tests pass.** Note: `sem_evaluate_expr.c` was also reformatted per
      `.clang-format` (editor format-on-save); no behavioural change.
- [x] A. `is_composite_kind()` (12-kind identifier-loading list) + `is_pointer_valued_kind()`
      (11-kind auto-load-exemption list) helpers in `type_descriptors.h`; replaced the
      duplicated kind lists in `llvm_ir_generator.c:312` and `ir_gen_postfix.c:1822/1858`.
      Kept two distinct helpers — the lists differ (MAYBE/VECTOR/MATRIX vs PROC/BIT_SET).
- [x] B. Moved `is_expression_wrapper_type` + `expression_unwrap_to_identifier` from
      `ir_gen_assign.c`/`llvm_ir_generator.h` into layer-neutral `ast_utils.c/h`; added
      `expression_unwrap_chain()`. Replaced the duplicate wrapper-walk loops in
      `sem_check.c` (both) and the inline loop in `ir_gen_postfix.c:1766`. The
      `ir_gen_assign.c:717/1070/1243` loops were left as-is (different semantics:
      single-step recursion / stop-at-postfix).
      **246/246 tests pass.**
- [x] C. `type_is_*` dispatch table in `polymorphism.c`: converted the 40-entry
      `strcmp` chain in `poly_eval_intrinsic` into a static
      `{name, bool(*)(TypeDescriptor const *)}` table (`intrinsic_predicates[]`),
      with `POLY_PRED`-generated predicate functions and a single linear scan.
      `type_base_type`/`type_elem_type` kept as direct string checks (different
      return shape: type_id). **246/246 tests pass.**
- [x] C. Matrix intrinsic extraction in `ir_gen_postfix.c`: extracted the
      inline 4-branch matrix-intrinsic dispatch block (transpose,
      outer_product, hadamard_product, matrix_flatten) in
      `ir_gen_postfix_call` into `ir_gen_postfix_matrix_intrinsic()`
      (returns bool consumed; sets *val/*cur_type). **246/246 tests pass.**
- [ ] D. Named-boolean conditions
- [ ] E. Large-function extraction
- [ ] F. Predicate extraction

## Verification

`cmake --build build` then `bash tests/run_tests.sh build/src/odinc` (246
tests, ~4 min timeout).
