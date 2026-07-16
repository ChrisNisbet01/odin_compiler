#include "ast_utils.h"

#include "odin_grammar_ast.h"

bool
is_type_node(odin_grammar_node_t * node)
{
    if (node == NULL) return false;
    switch (node->type)
    {
        case AST_NODE_BASIC_TYPE:
        case AST_NODE_POINTER_TYPE:
        case AST_NODE_ARRAY_TYPE:
        case AST_NODE_DYNAMIC_ARRAY_TYPE:
        case AST_NODE_SLICE_TYPE:
        case AST_NODE_TYPE_NAME:
        case AST_NODE_PROCEDURE_SIGNATURE:
        case AST_NODE_DISTINCT_TYPE:
        case AST_NODE_ENUM_TYPE:
        case AST_NODE_UNION_TYPE:
        case AST_NODE_STRUCT_TYPE:
        case AST_NODE_ENUM_TYPE_REF:
        case AST_NODE_STRUCT_TYPE_REF:
        case AST_NODE_MAP_TYPE:
        case AST_NODE_SOA_TYPE:
        case AST_NODE_BIT_FIELD_TYPE:
        case AST_NODE_BIT_SET_TYPE:
        case AST_NODE_MULTI_POINTER_TYPE:
        case AST_NODE_MAYBE_TYPE:
        case AST_NODE_VECTOR_TYPE:
        case AST_NODE_TUPLE_TYPE:
            return true;
        default:
            return false;
    }
}

odin_grammar_node_t *
node_find_child(odin_grammar_node_t * node, odin_grammar_node_type_t type)
{
    if (node == NULL) return NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL && node->list.children[i]->type == type)
            return node->list.children[i];
    }
    return NULL;
}

odin_grammar_node_t *
node_find_op(odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL) continue;
        switch (child->type)
        {
            case AST_NODE_UNARY_OP:
            case AST_NODE_MUL_OP:
            case AST_NODE_ADD_OP:
            case AST_NODE_SHIFT_OP:
            case AST_NODE_BIT_AND_OP:
            case AST_NODE_BIT_XOR_OP:
            case AST_NODE_BIT_OR_OP:
            case AST_NODE_COMP_OP:
            case AST_NODE_LOG_AND_OP:
            case AST_NODE_LOG_OR_OP:
            case AST_NODE_RANGE_OP:
            case AST_NODE_ASSIGN_OP:
                return child;
            default:
                break;
        }
    }
    return NULL;
}
