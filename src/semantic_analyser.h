#pragma once

#include "generator_lists.h"
#include "odin_grammar_ast.h"
#include "sem_error.h"
#include "type_descriptors.h"

typedef struct
{
    odin_grammar_node_t * ast;
    TypeDescriptors * type_registry;
    GeneratorContext * gen_ctx;
    SemErrorList errors;
} SemContext;

void sem_context_init(SemContext * ctx, odin_grammar_node_t * ast, TypeDescriptors * type_registry, GeneratorContext * gen_ctx);

bool sem_analyse(SemContext * ctx);
