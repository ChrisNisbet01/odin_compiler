#pragma once

#include "typed_value.h"

// Forward declaration to avoid a circular include with odin_grammar_ast.h
struct odin_grammar_node_t;

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

    // Polymorphic-procedure support.
    // is_polymorphic = true -> this symbol names a polymorphic proc whose
    //                          body has NOT been analyzed standalone.
    //                          call sites must resolve via `poly_resolve_call`
    //                          and produce a specialization.
    // NOTE: For Stage 1 the polymorphism machinery uses side tables keyed
    //       by `symbol_t*` rather than in-symbol metadata, to avoid shifting
    //       memory layout of the symbol_t struct (which exposes pre-existing
    //       use-after-free bugs in scope_lists.c / scope cleanup ordering).
    bool is_polymorphic;
} symbol_t;
