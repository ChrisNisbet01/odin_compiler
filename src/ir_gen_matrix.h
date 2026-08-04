#pragma once

#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

// Shared matrix memory-layout helpers.
//
// Matrices use a flat LLVM representation: [rows*columns x E]. The linear
// offset of element (row, col) depends on the declared layout:
//   column-major (default):  offset = row + col*rows
//   row-major   (#row_major): offset = col + row*cols

static inline int64_t
ir_gen_matrix_offset(int64_t row, int64_t col, int64_t rows, int64_t cols, bool is_row_major)
{
    return is_row_major ? (col + row * cols) : (row + col * rows);
}

// GEP address of element (row, col) given a pointer to the flat matrix.
static inline LLVMValueRef
ir_gen_matrix_elem_ptr(
    IrGenContext * ctx, LLVMValueRef matrix_ptr, TypeDescriptor const * matrix_type,
    int64_t row, int64_t col, char const * name
)
{
    if (matrix_ptr == NULL || matrix_type == NULL || matrix_type->llvm_type == NULL)
        return NULL;
    int64_t offset = ir_gen_matrix_offset(
        row, col, matrix_type->as.matrix.rows, matrix_type->as.matrix.columns,
        matrix_type->as.matrix.is_row_major);
    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false),
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)offset, false)
    };
    return LLVMBuildInBoundsGEP2(ctx->builder, matrix_type->llvm_type, matrix_ptr, indices, 2, name);
}

// GEP address of element at runtime (row_val, col_val) given a pointer to the
// flat matrix. Both index values must already be i64.
static inline LLVMValueRef
ir_gen_matrix_elem_ptr_runtime(
    IrGenContext * ctx, LLVMValueRef matrix_ptr, TypeDescriptor const * matrix_type,
    LLVMValueRef row_val, LLVMValueRef col_val, char const * name
)
{
    if (matrix_ptr == NULL || matrix_type == NULL || matrix_type->llvm_type == NULL
        || row_val == NULL || col_val == NULL)
        return NULL;
    int64_t rows = matrix_type->as.matrix.rows;
    int64_t cols = matrix_type->as.matrix.columns;
    LLVMValueRef row_times_cols = LLVMBuildMul(ctx->builder, row_val,
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)cols, false), "mm.rtc");
    LLVMValueRef col_times_rows = LLVMBuildMul(ctx->builder, col_val,
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)rows, false), "mm.ctr");
    // row-major:   offset = row*cols + col
    // column-major: offset = row + col*rows
    LLVMValueRef offset = matrix_type->as.matrix.is_row_major
                              ? LLVMBuildAdd(ctx->builder, row_times_cols, col_val, "mm.off")
                              : LLVMBuildAdd(ctx->builder, col_times_rows, row_val, "mm.off");
    // Flat array GEP: 2-index {0, offset}
    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false),
        offset
    };
    return LLVMBuildInBoundsGEP2(ctx->builder, matrix_type->llvm_type, matrix_ptr, indices, 2, name);
}

// Extract element (row, col) from a loaded flat matrix value.
static inline LLVMValueRef
ir_gen_matrix_elem_value(
    IrGenContext * ctx, LLVMValueRef matrix_val, TypeDescriptor const * matrix_type,
    int64_t row, int64_t col, char const * name
)
{
    if (matrix_val == NULL || matrix_type == NULL)
        return NULL;
    int64_t offset = ir_gen_matrix_offset(
        row, col, matrix_type->as.matrix.rows, matrix_type->as.matrix.columns,
        matrix_type->as.matrix.is_row_major);
    return LLVMBuildExtractValue(ctx->builder, matrix_val, (unsigned)offset, name);
}
