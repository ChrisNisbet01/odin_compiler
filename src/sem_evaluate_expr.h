#pragma once

#include "semantic_analyser.h"

TypeDescriptor const * sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node);
