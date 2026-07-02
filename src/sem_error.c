#include "sem_error.h"

#include <stdio.h>
#include <string.h>

void
sem_error_list_init(SemErrorList * list)
{
    list->count = 0;
}

bool
sem_error_list_has_errors(SemErrorList const * list)
{
    return list->count > 0;
}

void
sem_error_list_add(SemErrorList * list, char const * file_path, odin_grammar_node_t * node, char const * message)
{
    if (list->count >= MAX_SEM_ERRORS) return;
    list->errors[list->count].node = node;
    list->errors[list->count].file_path = file_path ? strdup(file_path) : NULL;
    list->errors[list->count].message = strdup(message);
    list->count++;
}

static void
print_location(FILE * f, SemError const * err)
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
sem_error_list_print(SemErrorList const * list)
{
    for (int i = 0; i < list->count; i++)
    {
        fprintf(stderr, "error: ");
        print_location(stderr, &list->errors[i]);
        fprintf(stderr, ": %s\n", list->errors[i].message);
    }
}
