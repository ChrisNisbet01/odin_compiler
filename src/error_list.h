#pragma once

#include "odin_grammar_ast.h"

#include <stdbool.h>

#define MAX_ERRORS 256

typedef struct
{
    char const * message;
    char const * file_path;
    odin_grammar_node_t * node;
} ErrorEntry;

typedef struct
{
    ErrorEntry errors[MAX_ERRORS];
    int count;
} ErrorList;

void error_list_init(ErrorList * list);

bool error_list_has_errors(ErrorList const * list);

void error_list_add(ErrorList * list, char const * file_path, odin_grammar_node_t * node, char const * message);

void error_list_print(ErrorList const * list);
