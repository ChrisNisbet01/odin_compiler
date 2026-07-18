#pragma once

#include "odin_grammar_ast.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>

// Postfix expression dispatch (called from ir_gen_node)
LLVMValueRef ir_gen_postfix_expression(IrGenContext * ctx, odin_grammar_node_t * node);
