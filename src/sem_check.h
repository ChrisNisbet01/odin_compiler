#pragma once

#include "semantic_analyser.h"

#include <stdbool.h>

bool sem_can_implicitly_convert(
    SemContext * ctx, odin_grammar_node_t * expr_node, TypeDescriptor const * from_type, TypeDescriptor const * to_type
);

bool sem_types_assignable(
    SemContext * ctx, odin_grammar_node_t * src_node, TypeDescriptor const * src_type, TypeDescriptor const * dst_type
);

void sem_check_assignment(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * target_type,
    TypeDescriptor const * src_type, odin_grammar_node_t * src_node
);
