#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

// Assignment / expression codegen (called from ir_gen_node)
LLVMValueRef ir_gen_assign_expression(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_assign_statement(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_or_else_expression(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_or_return_expression(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_ternary_expression(IrGenContext * ctx, odin_grammar_node_t * node);
