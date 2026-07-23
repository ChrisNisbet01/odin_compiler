#include "sem_check.h"

#include "sem_context.h"

#include <stdio.h>
#include <string.h>

bool
sem_can_implicitly_convert(
    SemContext * ctx, odin_grammar_node_t * expr_node, TypeDescriptor const * from_type, TypeDescriptor const * to_type
)
{
    (void)ctx;
    if (from_type == to_type)
        return true;
    if (from_type == NULL || to_type == NULL)
        return false;

    if (expr_node != NULL)
    {
        odin_grammar_node_t * inner = expr_node;
        while (inner != NULL && inner->list.count > 0
               && (inner->type == AST_NODE_EXPRESSION || inner->type == AST_NODE_ASSIGN_EXPRESSION
                   || inner->type == AST_NODE_OR_ELSE || inner->type == AST_NODE_TERNARY_EXPRESSION
                   || inner->type == AST_NODE_RANGE_EXPRESSION || inner->type == AST_NODE_LOG_OR_EXPRESSION
                   || inner->type == AST_NODE_LOG_AND_EXPRESSION || inner->type == AST_NODE_COMP_EXPRESSION
                   || inner->type == AST_NODE_BIT_OR_EXPRESSION || inner->type == AST_NODE_BIT_XOR_EXPRESSION
                   || inner->type == AST_NODE_BIT_AND_EXPRESSION || inner->type == AST_NODE_SHIFT_EXPRESSION
                   || inner->type == AST_NODE_ADD_EXPRESSION || inner->type == AST_NODE_MUL_EXPRESSION
                   || inner->type == AST_NODE_UNARY_EXPRESSION || inner->type == AST_NODE_POSTFIX_EXPRESSION
                   || inner->type == AST_NODE_PRIMARY_EXPRESSION))
        {
            inner = inner->list.children[0];
        }
        if (inner != NULL && inner->type == AST_NODE_INTEGER_VALUE
            && (is_integer_kind(to_type) || is_floating_kind(to_type)))
            return true;
        if (inner != NULL && inner->type == AST_NODE_FLOAT_VALUE
            && (is_floating_kind(to_type) || is_integer_kind(to_type)))
            return true;
    }
    return false;
}

bool
sem_types_assignable(
    SemContext * ctx, odin_grammar_node_t * src_node, TypeDescriptor const * src_type, TypeDescriptor const * dst_type
)
{
    if (src_type == dst_type)
        return true;
    if (src_type == NULL || dst_type == NULL)
        return false;
    if (sem_can_implicitly_convert(ctx, src_node, src_type, dst_type))
        return true;
    return false;
}

void
sem_check_assignment(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * target_type,
    TypeDescriptor const * src_type, odin_grammar_node_t * src_node
)
{
    if (target_type == NULL || src_type == NULL)
        return;
    if (target_type->kind != TD_KIND_DISTINCT && src_type->kind != TD_KIND_DISTINCT)
        return;
    if (sem_types_assignable(ctx, src_node, src_type, target_type))
        return;
    if (src_node != NULL)
    {
        odin_grammar_node_t * inner = src_node;
        while (inner != NULL && inner->list.count > 0
               && (inner->type == AST_NODE_EXPRESSION || inner->type == AST_NODE_ASSIGN_EXPRESSION
                   || inner->type == AST_NODE_PRIMARY_EXPRESSION))
        {
            inner = inner->list.children[0];
        }
        if (inner != NULL && inner->type == AST_NODE_AUTO_CAST_EXPR)
            return;
    }
    char tbuf[128], sbuf[128];
    type_write_canonical_name(target_type, tbuf, sizeof(tbuf));
    type_write_canonical_name(src_type, sbuf, sizeof(sbuf));
    char buf[256];
    snprintf(buf, sizeof(buf), "cannot assign '%s' to '%s'", sbuf, tbuf);
    sem_error_list_add(&ctx->errors, NULL, node, buf);
}
