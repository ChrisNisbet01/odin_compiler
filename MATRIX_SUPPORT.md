# Matrix Operation Support in Odin Compiler

## Overview

The official Odin compiler provides comprehensive support for matrix operations, treating matrices as first-class types with optimized implementations for common linear algebra operations.

## Matrix Type Syntax

```odin
matrix[R, C]T    // Row-major matrix with R rows, C columns, element type T
#row_major matrix[R, C]T  // Explicit row-major (default)
#column_major matrix[R, C]T  // Column-major storage
```

Example:
```odin
m: matrix[4, 4]f32 = {
    1, 2, 3, 4,
    5, 6, 7, 8,
    9, 10, 11, 12,
    13, 14, 15, 16,
}
```

## Supported Operations

### Arithmetic Operators

The `*` operator performs **matrix multiplication** for matrices:

```odin
// Matrix × Matrix (standard matrix multiplication)
result := a * b  // a is I×J, b is J×K, result is I×K

// Matrix × Vector (matrix-vector multiplication)
result := m * v  // m is I×J, v is [J], result is [I]

// Vector × Matrix (vector-matrix multiplication)
result := v * m  // v is [J], m is J×K, result is [K]
```

### Element-wise Operations

For element-wise operations, use the Hadamard product:

```odin
// Element-wise multiplication (Hadamard product)
result := hadamard_product(a, b)
// or
result := matrix_comp_mul(a, b)
```

### Other Operations

```odin
// Transpose
t := transpose(m)

// Identity matrix
I := identity(matrix[4, 4]f32)

// Determinant
d := determinant(m)

// Adjugate (classical adjoint)
adj := adjugate(m)

// Inverse
inv := inverse(m)

// Outer product (vector outer product)
result := outer_product(v1, v2)  // [N]T × [M]T → matrix[N, M]T

// Matrix flatten to array
arr := matrix_flatten(m)  // matrix[R, C]T → [R*C]T
```

## Type System Details

### Storage Order

- **`matrix[R, C]T`** (default): Row-major storage
- **`#row_major matrix[R, C]T`**: Explicit row-major
- **`#column_major matrix[R, C]T`**: Column-major storage

### Element Access

```odin
m[i, j]  // Access element at row i, column j
m[i]     // Access row i (returns array/row vector)
```

## Implementation in Official Compiler

### Type Descriptor (types.cpp)

```cpp
Type *alloc_type_matrix(Type *elem, i64 row_count, i64 column_count, 
                        Type *generic_row_count, Type *generic_column_count, 
                        bool is_row_major)
```

The matrix type stores:
- `elem`: Element type
- `row_count`, `column_count`: Dimensions
- `is_row_major`: Storage order
- Support for generic dimensions (`$R`, `$C`)

### Binary Operations (check_expr.cpp)

The `check_binary_matrix` function handles matrix operations:

1. **Matrix × Matrix**: Validates dimensions match and elements are identical
2. **Matrix × Array**: Treats arrays as column vectors
3. **Array × Matrix**: Treats arrays as row vectors
4. **Scalar × Matrix**: Broadcasts scalar to all elements

### Code Generation (llvm_backend_expr.cpp)

#### Matrix Multiplication (`lb_emit_matrix_mul`)

- Detects row-major vs column-major storage
- Uses SIMD vectorization when possible (`lb_is_matrix_simdable`)
- For square matrices with even dimensions: Uses optimized shuffle-based multiplication
- For general matrices: Iterates through rows and columns

#### Vector-Matrix Multiplication (`lb_emit_matrix_mul_vector`, `lb_emit_vector_mul_matrix`)

- Converts matrices to vectors for SIMD operations
- Uses shuffle and dot product intrinsics

#### Scalar Operations

- `matrix + matrix`: Element-wise addition
- `matrix - matrix`: Element-wise subtraction  
- `matrix * scalar`: Scalar multiplication (broadcast)
- `matrix / scalar`: Scalar division (broadcast)

## Comparison with Our Compiler

### Current State

 1. ✅ Matrix type syntax `[N][M]T` works (as nested arrays)
 2. ✅ Basic arithmetic operators work on element types
 3. ✅ Matrix multiplication (`*`) implemented
 4. ✅ Matrix × Scalar / Scalar × Matrix (broadcast) implemented
 5. ✅ Matrix + Matrix / Matrix - Matrix (element-wise) implemented
 6. ✅ Matrix / Scalar (broadcast) implemented
 7. ✅ Matrix × Vector / Vector × Matrix implemented
 8. ❌ No transpose, determinant, inverse functions
 9. ❌ No `#row_major` / `#column_major` directives
10. ❌ No SIMD optimization for matrix operations

### Missing Features

| Feature | Official Odin | Our Compiler |
|---------|--------------|--------------|
| `matrix[R, C]T` syntax | ✅ | ✅ (as `[R][C]T` nested array) |
| Matrix multiplication (`*`) | ✅ | ✅ |
| Matrix-vector multiply | ✅ | ✅ |
| Transpose | ✅ | ❌ |
| Determinant | ✅ | ❌ |
| Inverse | ✅ | ❌ |
| Row-major/column-major | ✅ | ❌ |
| SIMD matrix ops | ✅ | ❌ |

## Implementation Plan for Our Compiler

### Phase 1: Matrix Type Syntax

Add `matrix[R, C]T` as a distinct type (similar to how official Odin handles it):

```gdl
// Grammar additions needed
MatrixType = "matrix" LBracket CommaExpression RBracket CommaExpression RBracket TypePrefix
```

### Phase 2: Matrix Multiplication

Implement `check_binary_matrix` logic for:
- Matrix × Matrix (dot product based)
- Matrix × Vector (column vector)
- Vector × Matrix (row vector)

### Phase 3: Code Generation

Implement optimized LLVM IR generation:
- Use `LLVMBuildExtractValue` for element access
- Implement dot products for matrix multiplication
- Consider SIMD vectorization for performance

### Phase 4: Additional Operations

Add intrinsics for transpose, determinant, inverse via the `linalg` package approach.

## References

- Official test: `/home/chris/projects/Odin/tests/issues/test_issue_4584.odin`
- Matrix type definition: `src/types.cpp:1087`
- Binary matrix operations: `src/check_expr.cpp:4116`
- Matrix IR generation: `src/llvm_backend_expr.cpp:950`
- linalg package: `/home/chris/projects/Odin/core/math/linalg/general.odin`