#pragma once

#include "scope_lists.h"
#include "typed_value.h"

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct scope
{
    LLVMContextRef context;
    LLVMBuilderRef builder;

    scope_symbols_t symbols;

    struct scope * parent;
} scope_t;

scope_t * scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder);

void scope_free(scope_t * scope);

void scope_add_symbol(scope_t * scope, char const * name, TypedValue value);

symbol_t * scope_find_symbol_entry(scope_t const * scope, char const * name);
