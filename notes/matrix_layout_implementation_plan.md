# Matrix Memory Layout Implementation Plan

## Summary

This document tracks the implementation of `#row_major` / `#column_major` directives for matrix types in the Odin compiler, matching the real Odin language specification.

**Goal**: Support both column-major (default) and row-major memory layouts for matrices, controlled by the `#row_major` directive.

## Background: How Real Odin Matrices Work

From `/home/chris/projects/Odin` source analysis:

1. **LLVM representation**: Flat `[R*C x T]` array (NOT nested `[R x [C x T]]`)
2. **Linear indexing**: 
   - Column-major (default): `offset = row + col * R` 
   - Row-major: `offset = col + row * C`
3. **Directives**: `#row_major` or `#column_major` before a matrix type sets `MatrixType.is_row_major`
4. **Single-index access**: NOT supported — must use `m[row, col]` with both indices
5. **Matrix literal fill**: Literal element i → `row=i/cols, col=i%cols` → stores at computed offset (matches our current semantics)
6. **transpose**: Preserves `is_row_major`, swaps rows↔columns
7. **outer_product**: Result is column-major (`false`)
8. **flatten**: Returns array `[R*C]T` in memory order

## Our Current State

- **LLVM type**: `[R x [C x T]]` — row-major nested array
- **All GEP calls**: `[0, row, col]` — correct for current layout
- **`is_row_major` field**: Stored in TypeDescriptor but NEVER USED
- **Single-index `m[i]`**: Supported, returns row (NOT Odin-compliant)
- **Chained subscript `m[i][j]`**: Supported (widely used in tests)
- **Tests using chained subscript**: `test_matrix_basic.odin`, `test_matrix_mul.odin`, `test_matrix_vector.odin` (~40 usages total)

## Implementation Approach

Switch to **flat array representation** `[R*C x T]` matching real Odin. This:
- Makes both layouts trivial (same GEP `[0, offset]`, just different offset math)
- Makes `is_row_major` meaningful
- Simplifies `matrix_flatten` and all operators
- Aligns with real Odin for FFI compatibility

## Files to Modify

### 1. Grammar (`src/odin_grammar.gdl`)
- Add `KwRowMajor = lexeme("row_major" IdBoundary)` and `KwColumnMajor`
- Add both to `DirectiveName`
- Modify `MatrixType` rule:
  ```
  MatrixType = (Directive)? KwMatrix LBracket AssignExpression Comma AssignExpression RBracket TypePrefix @AST_ACTION_MATRIX_TYPE;
  ```

### 2. Type Descriptors (`src/type_descriptors.h`, `src/type_descriptors.c`)
- **CURRENTLY DEAD CODE**: `is_row_major` field (line 194 in .h, 1545 in .c)
- `get_or_create_matrix_type`: Change LLVM type from nested to flat:
  ```c
  td->llvm_type = LLVMArrayType(element_type->llvm_type, rows * columns);
  ```

### 3. Type Resolution (`src/sem_type_resolver.c`)
- `sem_resolve_matrix_type`: Handle optional leading `Directive` child
- Extract `is_row_major` from directive text (`#row_major` → true, `#column_major` → false)
- Pass `is_row_major` to `get_or_create_matrix_type` (currently hardcoded `true`)

### 4. Matrix Literal (`src/sem_evaluate_expr.c`, `src/llvm_ir_generator.c`)
- `sem_evaluate_matrix_lit_expr`: Currently correct — fills in mathematical row-major order
- `ir_gen_matrix_lit_expr`: Change from nested `InsertValue` at `[row, col]` to flat `InsertValue` at computed `offset` (using same formula as Odin: `row + col * rows` for column-major, etc.)

### 5. Semantic Subscript Handling (`src/sem_evaluate_expr.c`)
- **NEW BEHAVIOR**: Matrix subscript `m[i][j]` → compile error "matrix index requires both row and column"
- **NEW BEHAVIOR**: Matrix subscript `m[i]` → compile error "matrix index requires both row and column"
- Single-index access no longer supported (matches real Odin)

### 6. IR Generator — New Helper
Create shared helper in `src/ir_gen_postfix.c` (or new file):

```c
// Compute linear offset: col-major: row + col*R; row-major: col + row*C
static LLVMValueRef
ir_gen_matrix_offset(IrGenContext *ctx, TypeDescriptor const *mtx,
                     LLVMValueRef row_idx, LLVMValueRef col_idx)
{
    LLVMValueRef rows = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 
                                     mtx->as.matrix.rows, false);
    LLVMValueRef cols = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                                     mtx->as.matrix.columns, false);
    
    LLVMValueRef offset;
    if (mtx->as.matrix.is_row_major) {
        // row-major: offset = col + row * cols
        LLVMValueRef row_col[] = {rows, cols};
        offset = LLVMBuildAdd(ctx->builder, col_idx,
                             LLVMBuildMul(ctx->builder, row_idx, rows, ""), "");
    } else {
        // col-major: offset = row + col * rows
        offset = LLVMBuildAdd(ctx->builder, row_idx,
                             LLVMBuildMul(ctx->builder, col_idx, cols, ""), "");
    }
    return offset;
}

// GEP to element pointer given row/col
static LLVMValueRef
ir_gen_matrix_elem_ptr(IrGenContext *ctx, TypeDescriptor const *mtx,
                       LLVMValueRef base_ptr, LLVMValueRef row_idx,
                       LLVMValueRef col_idx, char const *name)
{
    LLVMValueRef offset = ir_gen_matrix_offset(ctx, mtx, row_idx, col_idx);
    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false),
        offset
    };
    return LLVMBuildInBoundsGEP2(ctx->builder, mtx->llvm_type, base_ptr,
                                  indices, 2, name);
}
```

### 7. IR Generation Sites (multiple files)

All use pattern: 3-index GEP `[0, row_idx, col_idx]` → change to helper call `[0, offset]`:

| File | Function | Lines | Notes |
|------|----------|-------|-------|
| `ir_gen_postfix.c` | `ir_gen_postfix_subscript` | ~872-970 | r-value `m[row,col]`, single-index [remove] |
| `ir_gen_postfix.c` | `ir_gen_postfix_transpose` | ~10-59 | Read source, write result[j,i] |
| `ir_gen_postfix.c` | `ir_gen_postfix_outer_product` | ~84-133 | Uses `[i,j]` without leading 0 (bug?) |
| `ir_gen_postfix.c` | `ir_gen_postfix_hadamard_product` | ~135-209 | Nested loops |
| `ir_gen_postfix.c` | `ir_gen_postfix_matrix_flatten` | ~211-247 | Extract row-by-row, reinsert in flat order |
| `ir_gen_assign.c` | `ir_gen_lvalue_matrix_subscript` | ~327-424 | l-value `m[i,j] = val` |
| `ir_gen_operator.c` | `ir_gen_matrix_mul` | ~302-376 | Triple nested loops |
| `ir_gen_operator.c` | `ir_gen_matrix_scalar_op` | ~378-422 | Element-wise |
| `ir_gen_operator.c` | `ir_gen_matrix_binop` | ~424-470 | Element-wise |
| `ir_gen_operator.c` | `ir_gen_matrix_vector_mul` | ~472-532 | |
| `ir_gen_operator.c` | `ir_gen_vector_matrix_mul` | ~534-594 | |

### 8. Dead Code to Remove (`src/ir_gen_postfix.c`)
- Lines 1044-1070: Second-subscript `TD_KIND_MATRIX` case (unreachable, has debug fprintf)

### 9. Tests
- **Update** `test_matrix_basic.odin`: Change `m[0][0]` → `m[0,0]` (40 occurrences)
- **Update** `test_matrix_mul.odin`: Change `m[i][j]` → `m[i,j]` 
- **Update** `test_matrix_vector.odin`: Change `m[i][j]` → `m[i,j]`
- **Update** `test_matrix_literal.odin`: Verify literal fill semantics (already correct)
- **Add** `test_matrix_row_major.odin`: Test `#row_major` directive and matrix literal

## Implementation Order

1. **Grammar**: Add `KwRowMajor`/`KwColumnMajor` lexemes, update `MatrixType` rule, update `DirectiveName`
2. **Type descriptor**: Change `get_or_create_matrix_type` LLVM type to flat, make `is_row_major` used
3. **Type resolution**: Handle optional directive, extract and propagate `is_row_major`
4. **Semantic analysis**: Update subscript to error on single-index, propagate `is_row_major` in ops
5. **Helper function**: Create `ir_gen_matrix_offset` and `ir_gen_matrix_elem_ptr`
6. **IR gen**: Update all matrix GEP sites to use helper
7. **Remove dead code**: Delete unreachable second-subscript case
8. **Tests**: Update chained subscript tests, add row-major test
9. **Verify**: Run full test suite

## Key Decision Points

1. **Single-index `m[i]`**: Dropping it for matrices (Odin-compliant)
2. **Chained subscript `m[i][j]`**: Dropping it (Odin requires `m[i,j]`)
3. **Result of matrix ops**: Preserve LHS's `is_row_major` (matrix×matrix, arithmetic)

## Status

- [x] Research real Odin matrix implementation
- [x] Analyze current compiler matrix implementation
- [ ] Write plan to this file
- [ ] Grammar changes
- [ ] Type descriptor changes
- [ ] Type resolution changes
- [ ] Semantic analysis changes
- [ ] IR generation helper
- [ ] IR generation site updates
- [ ] Dead code removal
- [ ] Test updates
- [ ] Full test suite verification