#pragma once

#include "semantic_analyser.h"

TypeDescriptor const * sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_struct_type(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_enum_type(SemContext * ctx, odin_grammar_node_t * node);
TypeDescriptor const * sem_resolve_union_type(SemContext * ctx, odin_grammar_node_t * node);

int collect_parameters_from_param_list(
    odin_grammar_node_t * param_list, odin_grammar_node_t ** out, int max_params);
