#pragma once

#include "odin_grammar_ast.h"

bool is_type_node(odin_grammar_node_t * node);

odin_grammar_node_t * node_find_child(odin_grammar_node_t * node, odin_grammar_node_type_t type);

odin_grammar_node_t * node_find_op(odin_grammar_node_t * node);
