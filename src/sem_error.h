#pragma once

#include "error_list.h"

typedef ErrorList SemErrorList;

static inline void
sem_error_list_init(SemErrorList * list)
{
    error_list_init(list);
}

static inline bool
sem_error_list_has_errors(SemErrorList const * list)
{
    return error_list_has_errors(list);
}

static inline void
sem_error_list_add(SemErrorList * list, char const * file_path, odin_grammar_node_t * node, char const * message)
{
    error_list_add(list, file_path, node, message);
}

static inline void
sem_error_list_print(SemErrorList const * list)
{
    error_list_print(list);
}
