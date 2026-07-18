#pragma once

#include "semantic_analyser.h"

TypeDescriptor const * sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node);
