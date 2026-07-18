#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

LLVMValueRef ir_gen_binary_expression(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_unary_expression(IrGenContext * ctx, odin_grammar_node_t * node);
