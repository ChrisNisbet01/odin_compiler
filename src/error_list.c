#include "error_list.h"

#include <stdio.h>
#include <string.h>

void
error_list_init(ErrorList * list)
{
    list->count = 0;
}

bool
error_list_has_errors(ErrorList const * list)
{
    return list->count > 0;
}

void
error_list_add(ErrorList * list, char const * file_path, odin_grammar_node_t * node, char const * message)
{
    if (list->count >= MAX_ERRORS) return;
    list->errors[list->count].node = node;
    char const * path = file_path;
    if (path == NULL && node != NULL && node->file_path != NULL)
        path = node->file_path;
    list->errors[list->count].file_path = path ? strdup(path) : NULL;
    list->errors[list->count].message = strdup(message);
    list->count++;
}

static void
print_location(FILE * f, ErrorEntry const * err)
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
error_list_print(ErrorList const * list)
{
    for (int i = 0; i < list->count; i++)
    {
        fprintf(stderr, "error: ");
        print_location(stderr, &list->errors[i]);
        fprintf(stderr, ": %s\n", list->errors[i].message);
    }
}
