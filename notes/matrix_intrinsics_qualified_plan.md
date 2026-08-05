# Plan: Restrict matrix intrinsics to package-qualified access (linalg.*)

## Goal

Make `transpose`, `outer_product`, `hadamard_product`, `matrix_flatten` only
resolvable via package qualification (`linalg.transpose(...)`), matching the
official Odin compiler. Currently our compiler accepts unqualified calls like
`transpose(m)` because the symbols leak into every file's scope.

## Root Cause

`stubs/base/runtime/runtime.odin` declares these four matrix intrinsics as
`@(builtin)` procs. The compiler auto-imports `base:runtime` as an implicit
`using` import into every file (`src/semantic_analyser.c:1769-1783`), copying
ALL runtime symbols into the global scope. Official Odin declares them only in
`base:intrinsics` (confirmed: official `base/runtime/runtime.odin` has none of
them) and exposes them via `linalg.transpose :: intrinsics.transpose`.

The matrix-intrinsic machinery is name-based interception:
- `sem_matrix_intrinsic_result_type` (`src/sem_evaluate_expr.c`)
- `ir_gen_name_matches` + `ir_gen_postfix_transpose` etc. (`src/ir_gen_postfix.c`)

so it works with or without the runtime symbol. Removing the symbol from
`base:runtime` only kills the unqualified resolution path; `linalg.transpose`
resolves through the linalg package scope (proven by `test_matrix_transpose.odin`,
which already uses `linalg.transpose` and passes).

## Changes

1. `stubs/base/runtime/runtime.odin` — remove the 4 matrix intrinsic
   declarations (currently lines 35-45): `transpose`, `outer_product`,
   `hadamard_product`, `matrix_flatten`. Their correct home remains
   `stubs/base/intrinsics/intrinsics.odin:67-70`; `general.odin:213` aliases them.

2. `tests/test_matrix_basic.odin:40` — `t := transpose(m)` → `t := linalg.transpose(m)`.

3. `tests/test_matrix_transpose_same_size.odin:15` — `t1 := transpose(m1)` →
   `t1 := linalg.transpose(m1)`.

4. New `tests/expected_to_fail/test_matrix_unqualified_transpose.odin` — locks in
   the official-Odin behavior: unqualified `transpose` must fail to compile.

5. Comment cleanup:
   - `src/sem_evaluate_expr.c:1993` — references runtime.odin for these procs; update.
   - `src/ir_gen_postfix.c:506` — "Runtime declares matrix_flatten as proc(m: any) -> any."; update.

6. `stubs/core/math/linalg/general.odin:1196` (`At := transpose(A)` inside
   package linalg) — uses the package-local alias; NO change needed, but must
   keep working after the removal.

## Verification

1. `cmake --build build`
2. Confirm unmodified `tests/test_matrix_basic.odin` FAILS to compile with
   "undeclared identifier: transpose" (matches official Odin behavior).
3. Edit test to `linalg.transpose`, recompile → passes.
4. Full suite: `bash tests/run_tests.sh build/src/odinc` — 246 tests
   (245 + 1 new expected_to_fail) must pass.
5. Confirm `linalg.transpose`, `linalg.determinant`, `linalg.matrix_flatten`
   still work; unqualified usage now errors.
