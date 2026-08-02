# Plan: Fix Short Variable Declaration (:=) for Poly Call Results

## Problem
When using short variable declaration `d := determinant(n)` where `determinant` is an overload bundle with poly candidates, the variable `d` gets an invalid type (kind=0, NULL) and subsequent subscript operations fail.

## Root Cause (Confirmed)
- The `:=` variable declaration is not correctly capturing the return type from poly-specialized calls to overload bundles
- `op->resolved_type` is being set during semantic analysis, but when used in a `:=` declaration, it's not properly propagated to the IR generation phase
- Debug output shows `cur_type=0x...` with `kind=0` (invalid type)

## Investigation Log
- Test `t := linalg.transpose(m)` followed by `t[0][0]` produces:
  ```
  DEBUG: Cannot subscript: cur_type=0x64dac7f048a0, kind=0
  error: cannot subscript type: not an array, slice, dynamic array, multi-pointer, or map
  ```
- This indicates the type descriptor is NULL

## Fix Location
- File: `src/sem_evaluate_expr.c` - how `POSTFIX_CALL` resolves types for bundle dispatch
- File: `src/ir_gen_variable_decl.c` - how `:=` declarations capture the expression result type
- The type resolution path for `linalg.transpose(m)` when `linalg` is a qualified import

## Solution Approach
1. Trace the type resolution chain for qualified calls (`linalg.transpose(m)`)
2. Verify `op->resolved_type` is set after bundle dispatch in sem
3. Check if IR gen uses the correct type when creating the variable
4. Ensure the poly-specialized proc's return type is the one being used