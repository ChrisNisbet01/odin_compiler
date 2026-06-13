#include "generator_lists.h"

#include <stdlib.h>

#define INITIAL_SCOPE_CAPACITY 64

GeneratorContext *
generator_context_create(
    LLVMContextRef context, LLVMBuilderRef builder, TypeDescriptors * type_registry
)
{
    GeneratorContext * gc = calloc(1, sizeof(GeneratorContext));
    if (gc == NULL) return NULL;

    gc->context = context;
    gc->builder = builder;
    gc->type_registry = type_registry;

    gc->scopes = malloc((size_t)INITIAL_SCOPE_CAPACITY * sizeof(scope_t *));
    if (gc->scopes == NULL)
    {
        free(gc);
        return NULL;
    }
    gc->capacity = INITIAL_SCOPE_CAPACITY;
    gc->count = 0;

    gc->global_scope = scope_create(NULL, context, builder);
    if (gc->global_scope == NULL)
    {
        free(gc->scopes);
        free(gc);
        return NULL;
    }

    return gc;
}

void
generator_context_destroy(GeneratorContext * gc)
{
    if (gc == NULL) return;

    for (int i = 0; i < gc->count; i++)
    {
        scope_free(gc->scopes[i]);
    }
    scope_free(gc->global_scope);
    free(gc->scopes);
    free(gc);
}

scope_t *
generator_push_scope(GeneratorContext * gc)
{
    if (gc == NULL) return NULL;

    scope_t * parent = gc->count > 0
        ? gc->scopes[gc->count - 1]
        : gc->global_scope;

    scope_t * new_scope = scope_create(parent, gc->context, gc->builder);
    if (new_scope == NULL) return NULL;

    if (gc->count >= gc->capacity)
    {
        gc->capacity *= 2;
        scope_t ** new_scopes = realloc(gc->scopes, (size_t)gc->capacity * sizeof(scope_t *));
        if (new_scopes == NULL)
        {
            scope_free(new_scope);
            return NULL;
        }
        gc->scopes = new_scopes;
    }

    gc->scopes[gc->count++] = new_scope;
    return new_scope;
}

void
generator_pop_scope(GeneratorContext * gc)
{
    if (gc == NULL || gc->count <= 0) return;
    gc->count--;
    scope_free(gc->scopes[gc->count]);
    gc->scopes[gc->count] = NULL;
}

scope_t *
generator_current_scope(GeneratorContext * gc)
{
    if (gc == NULL) return NULL;
    if (gc->count > 0) return gc->scopes[gc->count - 1];
    return gc->global_scope;
}

void
generator_add_symbol(GeneratorContext * gc, char const * name, TypedValue value)
{
    scope_t * scope = generator_current_scope(gc);
    if (scope) scope_add_symbol(scope, name, value);
}

symbol_t *
generator_find_symbol(GeneratorContext * gc, char const * name)
{
    scope_t * scope = generator_current_scope(gc);
    if (scope) return scope_find_symbol_entry(scope, name);
    return NULL;
}
