# Matrix Literal Support — Implementation Plan

## Goal
Add support for inline matrix literal syntax: `matrix[R,C]T{e1, e2, ..., eN}`

## Odin Language Reference
- Matrix literals use the same flat element list as array literals
- Syntax: `matrix[2,2]f64{1, 2, 3, 4}` — elements in memory order
- Empty literal `{}` is valid (zero-initialized)
- Element count must equal `rows * columns`
- Elements are filled in row-major order by default
- Also supports bare `{...}` form when type is known from declaration context

## Implementation Steps

### Step 1: Grammar — Add `MatrixLitExpr` rule
**File:** `src/odin_grammar.gdl`

Add a new rule mirroring `ArrayLitExpr` but using `MatrixType`:
```
MatrixLitExpr = MatrixType LBrace MatrixLitElements? RBrace @AST_ACTION_MATRIX_LIT_EXPR;
MatrixLitElement = StructLitExpr | MatrixLitExpr | ArrayLitExpr | AssignExpression;
MatrixLitElements = delimited_flex(MatrixLitElement, Comma) Comma? @AST_ACTION_MATRIX_LIT_ELEMENTS;
```

Add `MatrixLitExpr` to `ExpressionOrStructLit` (alongside `StructLitExpr | ArrayLitExpr | Expression`).
This enables matrix literals in variable declarations, assignments, and function arguments.

### Step 2: AST — Add node types and actions
**Files:** `src/odin_grammar_ast.h`, `src/odin_grammar_ast_actions.c`, `src/ast_node_name.c`

- Add `AST_NODE_MATRIX_LIT_EXPR` and `AST_NODE_MATRIX_LIT_ELEMENTS` enums
- Add `DEFINE_ACTION` entries for both
- Add node name strings for debugging

### Step 3: Semantic Analysis — Evaluate matrix literals
**File:** `src/sem_evaluate_expr.c`

Add `sem_evaluate_matrix_lit_expr()`:
1. Resolve first child (MatrixType) via `sem_resolve_type_expr` to get `TD_KIND_MATRIX` descriptor
2. Get rows/cols from the resolved type
3. Find optional `AST_NODE_MATRIX_LIT_ELEMENTS` child
4. Evaluate each element expression via `sem_evaluate_expr`
5. Validate element count == rows * columns
6. Set `node->resolved_type` to the matrix type

Add dispatch in `sem_evaluate_expr()` switch.

### Step 4: IR Generation — Emit matrix literal values
**File:** `src/llvm_ir_generator.c`

Add `ir_gen_matrix_lit_expr()`:
1. Create `LLVMGetUndef(matrix_llvm_type)` where matrix type is `[R x [C x T]]`
2. Iterate flat element list, for each element at index `i`:
   - Compute `row = i / cols`, `col = i % cols`
   - Call `ir_gen_node` to get element value
   - Coerce to element type if needed via `coerce_value_to_type`
   - `LLVMBuildInsertValue(val, elem, row)` → row value
   - `LLVMBuildInsertValue(row_val, elem, col)` → into matrix
3. Return the fully-constructed matrix value

Add dispatch in `ir_gen_node()` switch.

### Step 5: Tests
**File:** `tests/test_matrix_literal.odin`

Test cases:
- Basic 2x2 int literal
- 3x3 int literal
- Non-square 2x3 literal
- f64 literal
- Empty literal (zero-init)
- Matrix literal as function argument
- Matrix literal in expression context
- Element count mismatch → compile error (expected_to_fail)

## Key Files
| File | Change |
|------|--------|
| `src/odin_grammar.gdl` | Add MatrixLitExpr, MatrixLitElements, MatrixLitElement rules; update ExpressionOrStructLit |
| `src/odin_grammar_ast.h` | Add AST_NODE_MATRIX_LIT_EXPR, AST_NODE_MATRIX_LIT_ELEMENTS |
| `src/odin_grammar_ast_actions.c` | Add DEFINE_ACTION entries |
| `src/ast_node_name.c` | Add node name strings |
| `src/ast_utils.c` | Add is_type_node_table entry if needed |
| `src/sem_evaluate_expr.c` | Add sem_evaluate_matrix_lit_expr + dispatch |
| `src/llvm_ir_generator.c` | Add ir_gen_matrix_lit_expr + dispatch |
| `tests/test_matrix_literal.odin` | New test file |
| `notes/matrix_literal_plan.md` | This file |

## Reference: Existing Patterns
- Array literal: `ArrayLitExpr` grammar → `AST_NODE_ARRAY_LIT_EXPR` → `sem_evaluate_array_lit_expr` → `ir_gen_array_lit_expr`
- Matrix type resolution: `MatrixType` → `AST_NODE_MATRIX_TYPE` → `sem_resolve_matrix_type` → `get_or_create_matrix_type`
- Matrix LLVM layout: `[R x [C x T]]` via `LLVMArrayType(LLVMArrayType(T, C), R)`
