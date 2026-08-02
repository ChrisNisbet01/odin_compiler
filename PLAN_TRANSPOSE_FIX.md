# Plan: Fix Transpose IR Codegen Segfault

## Problem
When `transpose(m)` is called with `import using "core:math/linalg"`, the compiler crashes with SIGSEGV during IR generation. The crash occurs in LLVM's `DataLayout::getAlignment`.

## Root Cause (TBD)
The transpose special-case codegen in `ir_gen_postfix.c` may pass invalid LLVM types or values to LLVM functions.

## Investigation Steps
1. Run compiler with debugger to get exact crash location
2. Trace which LLVM type is NULL/invalid at crash time
3. Examine how `result_type->llvm_type` is populated for the transpose result when called from `import using` context

## Fix Location
- File: `src/ir_gen_postfix.c`
- Function: `ir_gen_postfix_transpose()`
- Lines around 226-285 (where transpose special-case is triggered)

## Solution Approach
1. Add comprehensive NULL checks for all LLVM types before use
2. Verify `op->resolved_type` is correctly set for using-import context
3. Consider whether the transpose special-case should be handled differently for package-qualified vs. bare calls