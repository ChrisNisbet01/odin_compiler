#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

void ir_gen_runtime_intrinsic_body(IrGenContext * ctx, char const * func_name,
                                   TypeDescriptor const * proc_type);

LLVMValueRef ir_gen_call_malloc(IrGenContext * ctx, LLVMValueRef size);
void ir_gen_call_free(IrGenContext * ctx, LLVMValueRef ptr);
LLVMValueRef ir_gen_call_calloc(IrGenContext * ctx, LLVMValueRef size);
LLVMValueRef ir_gen_call_strlen(IrGenContext * ctx, LLVMValueRef str_ptr);
