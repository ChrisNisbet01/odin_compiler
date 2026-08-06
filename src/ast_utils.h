#pragma once

#include <stdbool.h>

#include "odin_grammar_ast.h"

bool is_type_node(odin_grammar_node_t * node);

odin_grammar_node_t * node_find_child(odin_grammar_node_t * node, odin_grammar_node_type_t type);

odin_grammar_node_t * node_find_op(odin_grammar_node_t * node);

// Walk down the leftmost child chain through wrapper nodes (single-child expressions,
// primary expressions, etc.) to reach the innermost node. Stops at leaf nodes (no children).
odin_grammar_node_t * expression_chain_unwrap(odin_grammar_node_t * node);

// True for single-child expression wrapper nodes (chainl1 levels, primary
// expressions, etc.) that simply delegate to their first child.
bool is_expression_wrapper_type(odin_grammar_node_type_t type);

// Walk down the leftmost child chain through expression-wrapper nodes only.
// Unlike expression_chain_unwrap, stops as soon as a non-wrapper node is reached.
odin_grammar_node_t * expression_unwrap_chain(odin_grammar_node_t * node);

// Unwrap expression wrappers and return the node if it is an identifier.
odin_grammar_node_t * expression_unwrap_to_identifier(odin_grammar_node_t * node);

// Scan a parsed PROGRAM AST for a top-level `#+build ignore` directive.
bool ast_file_has_build_ignore(odin_grammar_node_t * program_ast);
