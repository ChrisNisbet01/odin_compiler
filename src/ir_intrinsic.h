#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

typedef void (*intrinsic_handler_fn)(IrGenContext * ctx, char const * func_name,
                                      TypeDescriptor const * proc_type);

void ir_gen_runtime_intrinsic_body(IrGenContext * ctx, char const * func_name,
                                   TypeDescriptor const * proc_type);

LLVMValueRef ir_gen_intrinsic_print_string(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_print_byte(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_int_to_string(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_os_exit(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_sys_write(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_sys_close(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_sys_open(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_sys_read(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_strings_to_string(IrGenContext * ctx);
LLVMValueRef ir_gen_intrinsic_strings_to_bytes(IrGenContext * ctx);
void ir_gen_intrinsic_builder_make_none(IrGenContext * ctx);
void ir_gen_intrinsic_free_all(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);

LLVMValueRef ir_gen_call_malloc(IrGenContext * ctx, LLVMValueRef size);
void ir_gen_call_free(IrGenContext * ctx, LLVMValueRef ptr);
LLVMValueRef ir_gen_call_calloc(IrGenContext * ctx, LLVMValueRef size);
LLVMValueRef ir_gen_call_strlen(IrGenContext * ctx, LLVMValueRef str_ptr);
LLVMValueRef ir_gen_call_mem_alloc(IrGenContext * ctx, LLVMValueRef size, LLVMValueRef alignment, LLVMValueRef allocator);
void ir_gen_call_mem_free(IrGenContext * ctx, LLVMValueRef ptr, LLVMValueRef allocator);
LLVMValueRef ir_gen_call_allocator_alloc(IrGenContext * ctx, LLVMValueRef allocator, LLVMValueRef size, LLVMValueRef alignment);
LLVMValueRef ir_gen_get_context_allocator(IrGenContext * ctx);

// Any introspection intrinsics (2-field struct)
void ir_gen_intrinsic_any_type_id(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);
void ir_gen_intrinsic_any_data_ptr(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);
void ir_gen_intrinsic_type_info_of(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);
void ir_gen_intrinsic_array_element(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);
void ir_gen_intrinsic_matrix_element(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type);
