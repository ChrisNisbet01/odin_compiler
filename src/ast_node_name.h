#pragma once

#include "odin_grammar_ast.h"

char const * get_node_type_name_from_node(odin_grammar_node_t const * node);

char const * get_node_type_name_from_type(odin_grammar_node_type_t node_type);

void debug_print_ast(odin_grammar_node_t const * node);
