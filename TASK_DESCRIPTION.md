# Current Task: Fix test_if_assign.odin

## Problem
The test `test_if_assign.odin` fails with exit code 246. The test expects that after an assignment inside an if block, the variable is updated and the final result is 0. However, the generated LLVM IR shows that the assignment inside the if block is completely missing (the then block is empty).

## Test Code
```odin
package main

main :: proc() -> int {
    x: int = 5
    y: int = 10
    if x < 10 {
        y = 20
    }
    return y - 20
}
```

## Expected Behavior
- `x` is 5, so `x < 10` is true.
- Inside the if block, `y` is assigned 20.
- After the if block, `y` is 20.
- `y - 20` equals 0, so the program should return 0.

## Actual Behavior
The generated IR for the then block is empty:
```
then:                                             ; preds = %entry
  br label %ifmerge
```
The assignment `y = 20` is not present in the IR.

## Hypothesis
The assignment statement inside the if block is not being processed during semantic analysis or IR generation. Possible causes:
1. The if statement's then-body is not being traversed for semantic analysis.
2. The assignment expression is not being recognized as a statement that needs IR generation.
3. The IR generation for assignment statements is broken.

## Steps to Fix
1. Examine `semantic_analyser.c` for handling of `AST_NODE_IF_STATEMENT` to ensure the then-body and else-body are analyzed.
2. Check that assignment statements (likely `AST_NODE_ASSIGN_STATEMENT`) are being processed in the statement analysis.
3. Verify that `ir_gen_assign_statement` (or similar) is called and correctly generates IR for the assignment.
4. If necessary, add debug prints to see if the assignment node is being visited.

## General Task: Fix All Broken Tests
After fixing `test_if_assign.odin`, we will proceed to fix the other failing tests in the following order (preferring non-hanging tests first):
1. test_if_assign.odin (exit code 246)
2. test_scope.odin (exit code 1)
3. test_switch.odin (exit code 1)
4. test_using.odin (exit code 28)
5. test_struct.odin (exit code 202)
6. test_chained_member.odin (exit code 153)
7. test_array.odin (exit code 20)
8. test_defer.odin (exit code 124 - timeout/hang)
9. test_for.odin (exit code 124 - timeout/hang)

## Even More General Task: Complete Partiall-Implemented Functionality
Refer to `unimplemented_features.md` for partially implemented features that need completion. After all tests pass, we should address:
- break/continue do not emit defers
- Logical `&&` / `||` without short-circuit (note: we already fixed short-circuit for logical operators in a previous session, so this might be already done)
- Chained struct member access with `using`

We will update `unimplemented_features.md` as we complete each item.

---
This description is saved to track progress and ensure we stay focused on the task at hand.