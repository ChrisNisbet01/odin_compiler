#pragma once

#include "odin_grammar_ast.h"

bool is_type_node(odin_grammar_node_t * node);

odin_grammar_node_t * node_find_child(odin_grammar_node_t * node, odin_grammar_node_type_t type);

odin_grammar_node_t * node_find_op(odin_grammar_node_t * node);

// Walk down the leftmost child chain through wrapper nodes (single-child expressions,
// primary expressions, etc.) to reach the innermost node. Stops at leaf nodes (no children).
odin_grammar_node_t * expression_chain_unwrap(odin_grammar_node_t * node);
