# Chained Struct Member Access — Implementation Plan

## Problem

Chained member access like `v.inner.x` (where `inner` is a non-`using` struct field) fails in the IR generator. Two bugs exist in both `ir_gen_lvalue` and `ir_gen_postfix_expression`:

### Bug 1: Wrong type used for subsequent operations

Both lvalue and rvalue paths use `base->resolved_type` (the type of the primary expression) for **every** postfix operation in the chain. After the first member access, the current type changes (e.g., from `Outer` to `Inner`), but subsequent ops still look up fields in the outer struct. This means `v.inner.x` cannot find `x` because `x` is a field of `Inner`, not `Outer` (unless promoted via `using`).

Fix: Track `cur_type` through the chain, updating it after each member/subscript op.

### Bug 2: Premature load breaks GEP chain (rvalue path only)

In `ir_gen_postfix_expression`, after each POSTFIX_MEMBER or POSTFIX_SUBSCRIPT, the code immediately loads the result via `LLVMBuildLoad2`. For `a.b.c`:
1. `.b`: GEP → ptr to b, **LOAD** → struct value
2. `.c`: tries GEP on struct value → **FAIL** (GEP requires pointer)

Fix: Don't load inside the postfix ops loop. Keep the pointer. At the end of the loop, load only if the final type is non-composite (scalar).

## Changes

### `llvm_ir_generator.c`

#### `ir_gen_lvalue` (lvalue path)
- Add `TypeDescriptor const * cur_type` set to `base->resolved_type` initially
- In `AST_NODE_POSTFIX_MEMBER`: use `cur_type` instead of `base->resolved_type`; after GEP, update `cur_type` to final field type
- In `AST_NODE_POSTFIX_SUBSCRIPT`: use `cur_type` instead of `base->resolved_type`; after GEP, update `cur_type` to element type

#### `ir_gen_postfix_expression` (rvalue path)
- Add `TypeDescriptor const * cur_type` set to `base->resolved_type` initially
- In `AST_NODE_POSTFIX_MEMBER`: use `cur_type` for field lookup; GEP to pointer (no load); update `cur_type`
- In `AST_NODE_POSTFIX_SUBSCRIPT`: use `cur_type` for array type; GEP to element pointer (no load); update `cur_type`
- In `AST_NODE_POSTFIX_CALL`: update `cur_type` to return type (existing logic mostly unchanged)
- After the postfix ops loop: if result is a pointer and `cur_type` is non-composite (not TD_KIND_STRUCT/ARRAY/SLICE), load the value

### `test/test_chained_member.odin`
- Define nested structs: `Inner { x: int }`, `Outer { inner: Inner }`
- Test: write `v.inner.x = 42`, read `v.inner.x`
- Test: multi-level: `A { b: B }`, `B { c: C }`, `C { d: int }`
- Verify return values

## Verification
- Build: `cmake --build build`
- Run tests: `tests/run_tests.sh`
- All 18 existing tests should still pass
- New test should pass
