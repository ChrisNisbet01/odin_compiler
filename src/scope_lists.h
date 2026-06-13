#pragma once

#include "generic_hash_table.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdbool.h>

typedef struct
{
    generic_hash_table_t * by_name;
} scope_symbols_t;

bool scope_symbols_init(scope_symbols_t * list);

void scope_symbols_free(scope_symbols_t * list);

void scope_symbols_add_entry(scope_symbols_t * list, char const * name, TypedValue value);

symbol_t * scope_symbols_lookup_entry_by_name(scope_symbols_t const * list, char const * name);
