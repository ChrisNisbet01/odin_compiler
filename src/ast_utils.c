#include "ast_utils.h"

#include <string.h>

#include "odin_grammar_ast.h"

static bool const is_type_node_table[AST_NODE_COUNT] = {
    [AST_NODE_BASIC_TYPE] = true,
    [AST_NODE_POINTER_TYPE] = true,
    [AST_NODE_ARRAY_TYPE] = true,
    [AST_NODE_DYNAMIC_ARRAY_TYPE] = true,
    [AST_NODE_SLICE_TYPE] = true,
    [AST_NODE_TYPE_NAME] = true,
    [AST_NODE_PROCEDURE_SIGNATURE] = true,
    [AST_NODE_DISTINCT_TYPE] = true,
    [AST_NODE_ENUM_TYPE] = true,
    [AST_NODE_UNION_TYPE] = true,
    [AST_NODE_STRUCT_TYPE] = true,
    [AST_NODE_ENUM_TYPE_REF] = true,
    [AST_NODE_STRUCT_TYPE_REF] = true,
    [AST_NODE_MAP_TYPE] = true,
    [AST_NODE_SOA_TYPE] = true,
    [AST_NODE_BIT_FIELD_TYPE] = true,
    [AST_NODE_BIT_SET_TYPE] = true,
    [AST_NODE_MULTI_POINTER_TYPE] = true,
    [AST_NODE_MAYBE_TYPE] = true,
    [AST_NODE_VECTOR_TYPE] = true,
    [AST_NODE_MATRIX_TYPE] = true,
    [AST_NODE_SPEC_TYPE] = true,
    [AST_NODE_TUPLE_TYPE] = true,
    [AST_NODE_TYPE_APPLICATION] = true,
    [AST_NODE_QUALIFIED_TYPE_NAME] = true,
    [AST_NODE_POLY_IDENT] = true,
};

bool
is_type_node(odin_grammar_node_t * node)
{
    if (node == NULL)
        return false;
    if ((size_t)node->type >= AST_NODE_COUNT)
        return false;
    return is_type_node_table[node->type];
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
expression_chain_unwrap(odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;
    while (node->list.count >= 1 && node->list.children[0] != NULL)
        node = node->list.children[0];
    return node;
}

// Recursively unwrap expression wrapper nodes to find the inner node.
// Wrapper nodes simply delegate to their first child.
bool
is_expression_wrapper_type(odin_grammar_node_type_t type)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (type)
    {
    case AST_NODE_EXPRESSION:
    case AST_NODE_ASSIGN_EXPRESSION:
    case AST_NODE_OR_ELSE:
    case AST_NODE_TERNARY_EXPRESSION:
    case AST_NODE_RANGE_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_UNARY_EXPRESSION:
    case AST_NODE_POSTFIX_EXPRESSION:
    case AST_NODE_PRIMARY_EXPRESSION:
        return true;
    default:
        return false;
    }

#pragma GCC diagnostic pop
}

odin_grammar_node_t *
expression_unwrap_chain(odin_grammar_node_t * node)
{
    while (node != NULL && is_expression_wrapper_type(node->type) && node->list.count > 0)
        node = node->list.children[0];
    return node;
}

odin_grammar_node_t *
expression_unwrap_to_identifier(odin_grammar_node_t * node)
{
    node = expression_unwrap_chain(node);
    if (node != NULL && node->type == AST_NODE_IDENTIFIER)
        return node;
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

bool
ast_file_has_build_ignore(odin_grammar_node_t * program_ast)
{
    if (program_ast == NULL)
        return false;

    // PROGRAM holds one EXTERNAL_DECLARATIONS child per parsed file
    // (single files have exactly one).
    for (size_t i = 0; i < program_ast->list.count; i++)
    {
        odin_grammar_node_t * ext = program_ast->list.children[i];
        if (ext == NULL || ext->type != AST_NODE_EXTERNAL_DECLARATIONS)
            continue;

        for (size_t j = 0; j < ext->list.count; j++)
        {
            odin_grammar_node_t * decl = ext->list.children[j];
            if (decl == NULL || decl->type != AST_NODE_BUILD_DIRECTIVE)
                continue;
            for (size_t k = 0; k < decl->list.count; k++)
            {
                odin_grammar_node_t * tag = decl->list.children[k];
                if (tag != NULL && tag->type == AST_NODE_BUILD_TAG && tag->text != NULL
                    && strcmp(tag->text, "ignore") == 0)
                    return true;
            }
        }
    }
    return false;
}
