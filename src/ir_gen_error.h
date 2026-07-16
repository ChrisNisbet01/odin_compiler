#pragma once

#include "error_list.h"

typedef ErrorList IrGenErrorCollection;

static inline void
ir_gen_error_collection_init(IrGenErrorCollection * col)
{
    error_list_init(col);
}

static inline bool
ir_gen_error_collection_has_errors(IrGenErrorCollection const * col)
{
    return error_list_has_errors(col);
}

static inline void
ir_gen_error_collection_add(IrGenErrorCollection * col, char const * file_path, odin_grammar_node_t * node, char const * message)
{
    error_list_add(col, file_path, node, message);
}

static inline void
ir_gen_error_collection_print(IrGenErrorCollection const * col)
{
    error_list_print(col);
}
