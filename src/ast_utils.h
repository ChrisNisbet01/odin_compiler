#pragma once

#include <stdbool.h>

#include "odin_grammar_ast.h"

bool is_type_node(odin_grammar_node_t * node);

odin_grammar_node_t * node_find_child(odin_grammar_node_t * node, odin_grammar_node_type_t type);

odin_grammar_node_t * node_find_op(odin_grammar_node_t * node);

// Walk down the leftmost child chain through wrapper nodes (single-child expressions,
// primary expressions, etc.) to reach the innermost node. Stops at leaf nodes (no children).
odin_grammar_node_t * expression_chain_unwrap(odin_grammar_node_t * node);

// Scan a parsed PROGRAM AST for a top-level `#+build ignore` directive.
bool ast_file_has_build_ignore(odin_grammar_node_t * program_ast);
