#pragma once

#include "semantic_analyser.h"

TypeDescriptor const * sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_struct_type(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_enum_type(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_union_type(SemContext * ctx, odin_grammar_node_t * node);

int collect_parameters_from_param_list(
    odin_grammar_node_t * param_list, odin_grammar_node_t ** out, int max_params);

// Extract the name AST nodes and type node from a PARAMETER node.
// Handles multi-name params: "a, b: T" -> names {a, b}, type T.
// If all children are Identifiers, the last one is the type (e.g. "x: T").
// Fills names_out[0..ret-1] (capacity max_names) and *type_out.
// *is_poly_decl is set true for "$T: typeid" declarations (no Identifier names).
int sem_extract_param_names(
    odin_grammar_node_t * param, odin_grammar_node_t ** names_out, int max_names,
    odin_grammar_node_t ** type_out, bool * is_poly_decl);
