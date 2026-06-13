#include "scope_lists.h"

#include <stdlib.h>
#include <string.h>

static size_t
hash_djb2(void const * key)
{
    char const * str = (char const *)key;
    size_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++) != '\0')
    {
        hash = ((hash << 5) + hash) + (size_t)c;
    }
    return hash;
}

static bool
str_equals(void const * key1, void const * key2)
{
    return strcmp((char const *)key1, (char const *)key2) == 0;
}

static generic_hash_table_key_ops_t tag_key_ops = {
    .hash = hash_djb2,
    .equals = str_equals,
};

static void
free_symbol(void * value, void * user_data)
{
    (void)user_data;
    symbol_t * symbol = (symbol_t *)value;
    if (symbol == NULL) return;
    free((void *)symbol->name);
    free(symbol);
}

bool
scope_symbols_init(scope_symbols_t * list)
{
    list->by_name = generic_hash_table_create(128, &tag_key_ops);
    return list->by_name != NULL;
}

void
scope_symbols_free(scope_symbols_t * list)
{
    if (list->by_name != NULL)
    {
        generic_hash_table_iterate(list->by_name, free_symbol, NULL);
        generic_hash_table_destroy(list->by_name);
        list->by_name = NULL;
    }
}

void
scope_symbols_add_entry(scope_symbols_t * list, char const * name, TypedValue value)
{
    if (list == NULL || name == NULL) return;

    symbol_t * existing = scope_symbols_lookup_entry_by_name(list, name);
    if (existing != NULL)
    {
        free((void *)existing->name);
        existing->name = strdup(name);
        existing->value = value;
        return;
    }

    symbol_t * new_symbol = calloc(1, sizeof(*new_symbol));
    if (new_symbol == NULL) return;
    new_symbol->name = strdup(name);
    new_symbol->value = value;
    generic_hash_table_insert(list->by_name, name, new_symbol);
}

symbol_t *
scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name)
{
    if (list == NULL || name == NULL) return NULL;
    return (symbol_t *)generic_hash_table_lookup(list->by_name, name);
}
