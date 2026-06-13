#include "scope.h"

#include <stdlib.h>
#include <string.h>

scope_t *
scope_create(scope_t * parent, LLVMContextRef context, LLVMBuilderRef builder)
{
    scope_t * scope = calloc(1, sizeof(*scope));
    if (scope == NULL) return NULL;

    scope->context = context;
    scope->builder = builder;

    if (!scope_symbols_init(&scope->symbols))
    {
        scope_free(scope);
        return NULL;
    }

    scope->parent = parent;
    return scope;
}

void
scope_free(scope_t * scope)
{
    if (scope == NULL) return;
    scope_symbols_free(&scope->symbols);
    free(scope);
}

void
scope_add_symbol(scope_t * scope, char const * name, TypedValue value)
{
    if (scope == NULL || name == NULL) return;
    scope_symbols_add_entry(&scope->symbols, name, value);
}

symbol_t *
scope_find_symbol_entry(scope_t const * scope, char const * name)
{
    if (name == NULL) return NULL;

    while (scope != NULL)
    {
        symbol_t * symbol = scope_symbols_lookup_entry_by_name(&scope->symbols, name);
        if (symbol != NULL) return symbol;
        scope = scope->parent;
    }

    return NULL;
}
