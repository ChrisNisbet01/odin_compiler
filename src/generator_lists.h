#pragma once

#include "scope.h"
#include "type_descriptors.h"
#include "typed_value.h"

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct
{
    scope_t ** scopes;
    int count;
    int capacity;
    scope_t * global_scope;
    LLVMContextRef context;
    LLVMBuilderRef builder;
    TypeDescriptors * type_registry;
} GeneratorContext;

GeneratorContext * generator_context_create(
    LLVMContextRef context, LLVMBuilderRef builder, TypeDescriptors * type_registry
);

void generator_context_destroy(GeneratorContext * gc);

scope_t * generator_push_scope(GeneratorContext * gc);

void generator_pop_scope(GeneratorContext * gc);

scope_t * generator_current_scope(GeneratorContext * gc);

void generator_add_symbol(GeneratorContext * gc, char const * name, TypedValue value);

symbol_t * generator_find_symbol(GeneratorContext * gc, char const * name);
