#pragma once

#include "odin_grammar_ast.h"

#include <stdbool.h>

#define MAX_IR_GEN_ERRORS 256

typedef struct
{
    char const * message;
    odin_grammar_node_t * node;
} IrGenError;

typedef struct
{
    IrGenError errors[MAX_IR_GEN_ERRORS];
    int count;
} IrGenErrorCollection;

void ir_gen_error_collection_init(IrGenErrorCollection * col);

bool ir_gen_error_collection_has_errors(IrGenErrorCollection const * col);

void ir_gen_error_collection_add(IrGenErrorCollection * col, odin_grammar_node_t * node, char const * message);

void ir_gen_error_collection_print(IrGenErrorCollection const * col);
