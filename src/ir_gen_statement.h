#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

void ir_gen_emit_defers_at_depth(IrGenContext * ctx, int depth);
void ir_gen_emit_defers_from_depth(IrGenContext * ctx, int min_depth);
void ir_gen_emit_all_defers(IrGenContext * ctx);
LLVMValueRef ir_gen_return_statement(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_compound_statement(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_if_statement(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_for_statement(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_switch_statement(IrGenContext * ctx, odin_grammar_node_t * node);
