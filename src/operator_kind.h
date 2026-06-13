#pragma once

typedef enum
{
    OP_INVALID = 0,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,

    OP_EQ,
    OP_NE,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_IN,
    OP_NOT_IN,

    OP_LOG_AND,
    OP_LOG_OR,
    OP_LOG_NOT,

    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_SHL,
    OP_SHR,

    OP_ASSIGN,
    OP_ADD_ASSIGN,
    OP_SUB_ASSIGN,
    OP_MUL_ASSIGN,
    OP_DIV_ASSIGN,
    OP_MOD_ASSIGN,
    OP_AND_ASSIGN,
    OP_OR_ASSIGN,
    OP_XOR_ASSIGN,
    OP_SHL_ASSIGN,
    OP_SHR_ASSIGN,

    OP_UNARY_NEG,
    OP_UNARY_POS,
    OP_UNARY_NOT,
    OP_UNARY_XOR,
    OP_UNARY_ADDR,
    OP_UNARY_DEREF,

    OP_RANGE,
    OP_RANGE_HALF,
} OperatorKind;
