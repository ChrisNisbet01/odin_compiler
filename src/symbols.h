#pragma once

#include "typed_value.h"

typedef enum
{
    SYMBOL_CONSTANT,
    SYMBOL_VARIABLE,
    SYMBOL_PROCEDURE,
    SYMBOL_TYPE,
} SymbolKind;

typedef struct symbol
{
    char const * name;
    SymbolKind kind;
    TypedValue value;
    bool is_private;
    long long const_int_val;
    bool has_const_int_val;
} symbol_t;
