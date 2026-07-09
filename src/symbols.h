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
} symbol_t;
