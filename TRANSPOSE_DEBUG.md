# Transpose Implementation Debug Notes

## Issue Summary
The transpose builtin function compiles but produces incorrect IR (default body with `unreachable`) instead of the custom transpose implementation.

## Current Implementation

### Semantic Analyser (`sem_evaluate_expr.c`)
- Detects `transpose(m)` calls in `sem_evaluate_postfix_expr`
- Checks if callee name is "transpose"
- Evaluates argument and checks if it's a matrix
- Sets `op->resolved_type` to transposed matrix dimensions
- Sets `op->resolved_symbol` to the transpose symbol

### IR Generator (`ir_gen_postfix.c`)
- Added `ir_gen_postfix_transpose()` helper function
- Added transpose handling in `ir_gen_postfix_call()` before `any` packing
- Checks function name is "transpose"
- Gets matrix type from `arg_types[0]`
- Loads matrix from alloca pointer
- Calls `ir_gen_postfix_transpose()` to generate transpose IR

### Runtime Declaration (`stubs/core/runtime/runtime.odin`)
```odin
@(builtin)
transpose :: proc(m: any) -> any ---
```

## Observed Behavior
- Function signature in IR: `define %any @transpose(ptr %0, %any %1)`
- Function body: store argument, then `unreachable`
- No actual transpose code generated

## Suspected Issues

### 1. Argument Type Propagation
The `any` packing code at lines 351-375 in `ir_gen_postfix_call()` modifies `args[pi]` but NOT `arg_types[pi]`. However, the transpose check is placed BEFORE this code, so `arg_types[0]` should still have the matrix type.

But wait - the semantic analyser sets `op->resolved_type`, not `arg_types[0]`. The IR generator gets `arg_types[0]` from `node->resolved_type` during argument collection.

### 2. Semantic Analyser Not Executed
The `sem_evaluate_postfix_expr` function handles postfix operations in a loop. For `transpose(m)`:
- The base is `transpose` (identifier)
- The postfix operation is `POSTFIX_CALL`

The issue might be that the semantic analyser code for `AST_NODE_POSTFIX_CALL` is not being reached, or `callee_sym` is NULL.

### 3. IR Generator Check Not Matching
In `ir_gen_postfix_call()`, the transpose check uses:
```c
char const * func_name = NULL;
if (op->resolved_symbol)
    func_name = op->resolved_symbol->llvm_name ? op->resolved_symbol->llvm_name : op->resolved_symbol->name;
```

But `op->resolved_symbol` might be NULL if the semantic analyser didn't set it correctly.

## Debug Plan

1. Add debug output to verify semantic analyser is reached
2. Check if `callee_sym` is set correctly in semantic analyser
3. Check if `op->resolved_type` is set correctly after semantic analyser
4. Verify `arg_types[0]` has matrix type in IR generator
5. Use gdb to step through the code

## GDB Commands to Try
```bash
gdb --args build/src/odinc run tests/test_matrix_transpose.odin
(gdb) break ir_gen_postfix_call
(gdb) break ir_gen_postfix_transpose
(gdb) break sem_evaluate_postfix_expr
(gdb) run
```