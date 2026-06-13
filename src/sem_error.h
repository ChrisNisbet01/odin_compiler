#pragma once

#include "odin_grammar_ast.h"

#include <stdbool.h>

#define MAX_SEM_ERRORS 256

typedef struct
{
    char const * message;
    odin_grammar_node_t * node;
} SemError;

typedef struct
{
    SemError errors[MAX_SEM_ERRORS];
    int count;
} SemErrorList;

void sem_error_list_init(SemErrorList * list);

bool sem_error_list_has_errors(SemErrorList const * list);

void sem_error_list_add(SemErrorList * list, odin_grammar_node_t * node, char const * message);

void sem_error_list_print(SemErrorList const * list);
