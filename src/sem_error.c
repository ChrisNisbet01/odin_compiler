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
sem_error_list_add(SemErrorList * list, odin_grammar_node_t * node, char const * message)
{
    if (list->count >= MAX_SEM_ERRORS) return;
    list->errors[list->count].node = node;
    list->errors[list->count].message = strdup(message);
    list->count++;
}

void
sem_error_list_print(SemErrorList const * list)
{
    for (int i = 0; i < list->count; i++)
    {
        fprintf(stderr, "Semantic error: %s\n", list->errors[i].message);
    }
}
