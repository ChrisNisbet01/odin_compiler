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
ir_gen_error_collection_add(IrGenErrorCollection * col, char const * file_path, odin_grammar_node_t * node, char const * message)
{
    if (col->count >= MAX_IR_GEN_ERRORS) return;
    col->errors[col->count].node = node;
    char const * path = file_path;
    if (path == NULL && node != NULL && node->file_path != NULL)
        path = node->file_path;
    col->errors[col->count].file_path = path ? strdup(path) : NULL;
    col->errors[col->count].message = strdup(message);
    col->count++;
}

static void
print_location(FILE * f, IrGenError const * err)
{
    if (err->file_path)
    {
        fprintf(f, "%s", err->file_path);
    }
    else
    {
        fprintf(f, "<unknown>");
    }
    if (err->node && err->node->source_data.view.line_number > 0)
    {
        fprintf(f, ":%zu:%zu", err->node->source_data.view.line_number, err->node->source_data.view.column_number);
    }
}

void
ir_gen_error_collection_print(IrGenErrorCollection const * col)
{
    for (int i = 0; i < col->count; i++)
    {
        fprintf(stderr, "error: ");
        print_location(stderr, &col->errors[i]);
        fprintf(stderr, ": %s\n", col->errors[i].message);
    }
}
