#include "ir_gen_error.h"

#include <stdio.h>
#include <string.h>

void
ir_gen_error_collection_init(IrGenErrorCollection * col)
{
    col->count = 0;
}

bool
ir_gen_error_collection_has_errors(IrGenErrorCollection const * col)
{
    return col->count > 0;
}

void
ir_gen_error_collection_add(IrGenErrorCollection * col, odin_grammar_node_t * node, char const * message)
{
    if (col->count >= MAX_IR_GEN_ERRORS) return;
    col->errors[col->count].node = node;
    col->errors[col->count].message = strdup(message);
    col->count++;
}

void
ir_gen_error_collection_print(IrGenErrorCollection const * col)
{
    for (int i = 0; i < col->count; i++)
    {
        fprintf(stderr, "IR gen error: %s\n", col->errors[i].message);
    }
}
