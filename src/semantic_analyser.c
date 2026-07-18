#include "semantic_analyser.h"

#include "ast_utils.h"
#include "package_resolver.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include "sem_context.h"
#include "sem_check.h"
#include "sem_type_resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for remaining static functions
static void sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type);
static void sem_pass1_register_top_level_ex(SemContext * ctx, odin_grammar_node_t * program_ast);
static void sem_pass2_analyse_bodies_ast(SemContext * ctx, odin_grammar_node_t * program);
static void sem_analyse_attributes(odin_grammar_node_t * decl_node);

// --- Compile-time constant integer evaluation ---
// Evaluates a constant expression to an integer at compile time.
// Returns the value and sets *ok = 1 on success, or sets *ok = 0 on failure.
long long
sem_evaluate_constant_int(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    if (node == NULL) { *ok = 0; return 0; }

    // Unwrap through expression chain to reach a node type we can evaluate
    while (1)
    {
        int can_eval = 0;
        switch (node->type)
        {
        case AST_NODE_BOOL_TRUE:
        case AST_NODE_BOOL_FALSE:
        case AST_NODE_INTEGER_VALUE:
        case AST_NODE_UNARY_EXPRESSION:
        case AST_NODE_COMP_EXPRESSION:
        case AST_NODE_ADD_EXPRESSION:
        case AST_NODE_MUL_EXPRESSION:
        case AST_NODE_BIT_AND_EXPRESSION:
        case AST_NODE_BIT_XOR_EXPRESSION:
        case AST_NODE_BIT_OR_EXPRESSION:
        case AST_NODE_SHIFT_EXPRESSION:
        case AST_NODE_LOG_AND_EXPRESSION:
        case AST_NODE_LOG_OR_EXPRESSION:
        case AST_NODE_IDENTIFIER:
            can_eval = 1;
            break;
        case AST_NODE_POSTFIX_EXPRESSION:
            // Don't unwrap if it has postfix member (e.g. os.O_WRONLY) — evaluate it
            if (node->list.count >= 2 && node->list.children[1] != NULL)
            {
                odin_grammar_node_t * postfix_ops = node->list.children[1];
                if (postfix_ops->list.count > 0
                    && postfix_ops->list.children[0] != NULL
                    && postfix_ops->list.children[0]->type == AST_NODE_POSTFIX_MEMBER)
                    can_eval = 1;
            }
            break;
        default:
            break;
        }
        if (can_eval)
            break;
        if ((node->type == AST_NODE_POSTFIX_EXPRESSION || node->list.count == 1) && node->list.children[0])
            node = node->list.children[0];
        else
            { *ok = 0; return 0; }
    }

    switch (node->type)
    {
    case AST_NODE_BOOL_TRUE:
        *ok = 1; return 1;
    case AST_NODE_BOOL_FALSE:
        *ok = 1; return 0;

    case AST_NODE_IDENTIFIER:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
        if (sym != NULL && sym->has_const_int_val)
        {
            *ok = 1;
            return sym->const_int_val;
        }
        *ok = 0;
        return 0;
    }

    case AST_NODE_POSTFIX_EXPRESSION:
    {
        // Package-qualified constant: e.g. os.O_WRONLY
        if (node->list.count < 2 || node->list.children[0] == NULL || node->list.children[1] == NULL)
        { *ok = 0; return 0; }

        odin_grammar_node_t * inner = node->list.children[0];
        // Unwrap to identifier
        while (inner != NULL && inner->type != AST_NODE_IDENTIFIER && inner->list.count >= 1)
            inner = inner->list.children[0];
        if (inner == NULL || inner->type != AST_NODE_IDENTIFIER)
        { *ok = 0; return 0; }

        ImportedPackage * pkg = find_imported_package_by_name(ctx, inner->text);
        if (pkg == NULL || pkg->package_scope == NULL)
        { *ok = 0; return 0; }

        odin_grammar_node_t * postfix_ops = node->list.children[1];
        if (postfix_ops == NULL || postfix_ops->list.count == 0)
        { *ok = 0; return 0; }

        odin_grammar_node_t * member_op = postfix_ops->list.children[0];
        if (member_op == NULL || member_op->type != AST_NODE_POSTFIX_MEMBER)
        { *ok = 0; return 0; }

        if (member_op->list.count < 1 || member_op->list.children[0] == NULL)
        { *ok = 0; return 0; }

        char const * member_name = member_op->list.children[0]->text;
        symbol_t * sym = scope_find_symbol_entry(pkg->package_scope, member_name);
        if (sym != NULL && sym->has_const_int_val)
        {
            *ok = 1;
            return sym->const_int_val;
        }
        *ok = 0;
        return 0;
    }

    case AST_NODE_INTEGER_VALUE:
    {
        if (node->text == NULL) { *ok = 0; return 0; }
        char * end = NULL;
        long long val = parse_odin_signed(node->text, &end, 0);
        if (end == node->text) { *ok = 0; return 0; }
        *ok = 1;
        return val;
    }

    case AST_NODE_UNARY_EXPRESSION:
    {
        odin_grammar_node_t * op_node = node_find_op(node);
        if (op_node == NULL) { *ok = 0; return 0; }
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL) { *ok = 0; return 0; }

        odin_grammar_node_t * operand = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child != NULL && child != op_node) { operand = child; break; }
        }
        if (operand == NULL) { *ok = 0; return 0; }

        int inner_ok = 0;
        long long inner_val = sem_evaluate_constant_int(ctx, operand, &inner_ok);
        if (!inner_ok) { *ok = 0; return 0; }

        switch (md->kind)
        {
        case OP_UNARY_NEG: *ok = 1; return -inner_val;
        case OP_UNARY_POS: *ok = 1; return inner_val;
        case OP_UNARY_XOR: *ok = 1; return ~inner_val;
        case OP_UNARY_NOT: *ok = 1; return inner_val ? 0 : 1;
        default: *ok = 0; return 0;
        }
    }

    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    {
        odin_grammar_node_t * op_node = node_find_op(node);
        if (op_node == NULL) { *ok = 0; return 0; }
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL) { *ok = 0; return 0; }
        if (node->list.count < 3) { *ok = 0; return 0; }

        int lhs_ok = 0, rhs_ok = 0;
        long long lhs_val = sem_evaluate_constant_int(ctx, node->list.children[0], &lhs_ok);
        long long rhs_val = sem_evaluate_constant_int(ctx, node->list.children[node->list.count - 1], &rhs_ok);
        if (!lhs_ok || !rhs_ok) { *ok = 0; return 0; }

        switch (md->kind)
        {
        case OP_ADD: *ok = 1; return lhs_val + rhs_val;
        case OP_SUB: *ok = 1; return lhs_val - rhs_val;
        case OP_MUL: *ok = 1; return lhs_val * rhs_val;
        case OP_DIV: if (rhs_val == 0) { *ok = 0; return 0; } *ok = 1; return lhs_val / rhs_val;
        case OP_MOD: if (rhs_val == 0) { *ok = 0; return 0; } *ok = 1; return lhs_val % rhs_val;
        case OP_SHL: *ok = 1; return lhs_val << rhs_val;
        case OP_SHR: *ok = 1; return lhs_val >> rhs_val;
        case OP_BIT_AND: *ok = 1; return lhs_val & rhs_val;
        case OP_BIT_OR:  *ok = 1; return lhs_val | rhs_val;
        case OP_BIT_XOR: *ok = 1; return lhs_val ^ rhs_val;
        case OP_EQ: *ok = 1; return (lhs_val == rhs_val) ? 1 : 0;
        case OP_NE: *ok = 1; return (lhs_val != rhs_val) ? 1 : 0;
        case OP_LT: *ok = 1; return (lhs_val < rhs_val) ? 1 : 0;
        case OP_GT: *ok = 1; return (lhs_val > rhs_val) ? 1 : 0;
        case OP_LE: *ok = 1; return (lhs_val <= rhs_val) ? 1 : 0;
        case OP_GE: *ok = 1; return (lhs_val >= rhs_val) ? 1 : 0;
        default: *ok = 0; return 0;
        }
    }

    default:
        *ok = 0;
        return 0;
    }
}

// Compile-time constant boolean evaluation.
// Returns:  1 = true, 0 = false, -1 = unknown (can't evaluate at compile time)
int
sem_evaluate_constant_bool(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return -1;
    int ok = 0;
    long long val = sem_evaluate_constant_int(ctx, node, &ok);
    if (!ok)
        return -1;
    return val ? 1 : 0;
}

#include "sem_evaluate_expr.h"

static void
sem_analyse_return_statement(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type)
{
    if (expected_return_type != NULL && expected_return_type->kind == TD_KIND_PROC)
    {
        ProcMetadata const * pm = &expected_return_type->proc_metadata;
        if (pm->return_count > 1)
        {
            size_t expr_count = node->list.count;
            if ((int)expr_count != pm->return_count)
            {
                sem_error_list_add(&ctx->errors, NULL, node, "wrong number of return values");
                return;
            }
            for (size_t i = 0; i < expr_count; i++)
            {
                odin_grammar_node_t * expr = node->list.children[i];
                if (expr == NULL)
                    continue;
                TypeDescriptor const * expr_type = sem_evaluate_expr(ctx, expr);
                {
                    odin_grammar_node_t * inner = expr;
                    while (
                        inner != NULL && inner->list.count > 0
                        && (inner->type == AST_NODE_ASSIGN_EXPRESSION || inner->type == AST_NODE_OR_ELSE
                            || inner->type == AST_NODE_TERNARY_EXPRESSION || inner->type == AST_NODE_RANGE_EXPRESSION
                            || inner->type == AST_NODE_LOG_OR_EXPRESSION || inner->type == AST_NODE_LOG_AND_EXPRESSION
                            || inner->type == AST_NODE_COMP_EXPRESSION || inner->type == AST_NODE_BIT_OR_EXPRESSION
                            || inner->type == AST_NODE_BIT_XOR_EXPRESSION || inner->type == AST_NODE_BIT_AND_EXPRESSION
                            || inner->type == AST_NODE_SHIFT_EXPRESSION || inner->type == AST_NODE_ADD_EXPRESSION
                            || inner->type == AST_NODE_MUL_EXPRESSION || inner->type == AST_NODE_UNARY_EXPRESSION
                            || inner->type == AST_NODE_POSTFIX_EXPRESSION || inner->type == AST_NODE_PRIMARY_EXPRESSION)
                    )
                    {
                        inner = inner->list.children[0];
                    }
                    if (inner != NULL && inner->type == AST_NODE_AUTO_CAST_EXPR)
                        continue;
                }
                if (expr_type != pm->returns[i] && !sem_can_implicitly_convert(ctx, expr, expr_type, pm->returns[i]))
                {
                    sem_error_list_add(&ctx->errors, NULL, node, "return type mismatch");
                }
            }
            return;
        }
        else if (pm->return_count == 1)
        {
            expected_return_type = pm->returns[0];
        }
        else
        {
            if (node->list.count > 0)
                sem_error_list_add(&ctx->errors, NULL, node, "unexpected return value in void procedure");
            return;
        }
    }

    if (node->list.count == 0)
    {
        if (expected_return_type != NULL && expected_return_type != type_descriptor_get_void_type(ctx->type_registry))
        {
            sem_error_list_add(&ctx->errors, NULL, node, "expected return value");
        }
        return;
    }

    odin_grammar_node_t * expr = node->list.children[0];
    TypeDescriptor const * expr_type = sem_evaluate_expr(ctx, expr);

    if (expected_return_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "unexpected return value in void procedure");
        return;
    }

    // Check if expression contains an auto_cast at any depth
    {
        bool has_auto_cast = false;
        odin_grammar_node_t * queue[32];
        int q_head = 0, q_tail = 0;
        queue[q_tail++] = expr;
        while (q_head < q_tail && !has_auto_cast)
        {
            odin_grammar_node_t * n = queue[q_head++];
            if (n->type == AST_NODE_AUTO_CAST_EXPR)
            {
                has_auto_cast = true;
                break;
            }
            for (size_t ci = 0; ci < n->list.count && (q_tail < 32); ci++)
            {
                if (n->list.children[ci] != NULL)
                    queue[q_tail++] = n->list.children[ci];
            }
        }
        if (has_auto_cast)
            return;
    }

    if (expr_type != expected_return_type && !sem_can_implicitly_convert(ctx, expr, expr_type, expected_return_type))
    {
        sem_error_list_add(&ctx->errors, NULL, node, "return type mismatch");
    }
}

static void
sem_analyse_compound_statement(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type
)
{
    for (size_t i = 0; i < node->list.count; i++)
    {
        sem_pass2_node(ctx, node->list.children[i], expected_return_type);
    }
}

static TypeDescriptor const *
sem_resolve_procedure_signature(SemContext * ctx, odin_grammar_node_t * node,
                                TypeDescriptor const *** out_param_types, int * out_param_count,
                                TypeDescriptor const ** out_return_type, int * out_return_count,
                                calling_convention_t * out_cc, bool * out_is_variadic)
{
    TypeDescriptor const * return_type = NULL;
    TypeDescriptor const ** return_types = NULL;
    int return_count = 0;
    int param_count = 0;
    TypeDescriptor const ** param_types = NULL;
    size_t param_cap = 0;
    calling_convention_t cc = CALLING_CONV_ODIN;
    bool is_variadic = false;

    odin_grammar_node_t * param_list_node = NULL;

    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_PROCEDURE_SIGNATURE)
        {
            for (size_t j = 0; j < child->list.count; j++)
            {
                odin_grammar_node_t * sig_child = child->list.children[j];
                if (sig_child == NULL)
                    continue;
                if (sig_child->type == AST_NODE_CALLING_CONVENTION && sig_child->list.count > 0)
                {
                    odin_grammar_node_t * str_child = sig_child->list.children[0];
                    if (str_child && str_child->text)
                        cc = parse_calling_convention(str_child->text);
                }
                else if (sig_child->type == AST_NODE_RETURNS && sig_child->list.count > 0)
                {
                    odin_grammar_node_t * ret_child = sig_child->list.children[0];
                    if (ret_child->type == AST_NODE_RETURN_TYPE_LIST)
                    {
                        return_type = NULL;
                        return_count = 0;
                        return_types = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
                        for (size_t ri = 0; ri < ret_child->list.count; ri++)
                        {
                            odin_grammar_node_t * tn = ret_child->list.children[ri];
                            if (tn == NULL)
                                continue;
                            TypeDescriptor const * td = sem_resolve_type_expr(ctx, tn);
                            tn->resolved_type = (TypeDescriptor *)td;
                            if (td)
                                return_types[return_count++] = td;
                        }
                    }
                    else if (ret_child->type == AST_NODE_RETURN_LIST)
                    {
                        return_type = NULL;
                        return_count = 0;
                        return_types = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
                        for (size_t ri = 0; ri < ret_child->list.count; ri++)
                        {
                            odin_grammar_node_t * named = ret_child->list.children[ri];
                            if (named == NULL || named->type != AST_NODE_NAMED_RETURN)
                                continue;
                            odin_grammar_node_t * tn = NULL;
                            for (size_t ci = 0; ci < named->list.count; ci++)
                            {
                                if (named->list.children[ci] != NULL)
                                    tn = named->list.children[ci];
                            }
                            if (tn == NULL)
                                continue;
                            TypeDescriptor const * td = sem_resolve_type_expr(ctx, tn);
                            tn->resolved_type = (TypeDescriptor *)td;
                            if (td)
                                return_types[return_count++] = td;
                        }
                    }
                    else
                    {
                        return_type = sem_resolve_type_expr(ctx, ret_child);
                        if (return_type)
                        {
                            return_types = malloc(sizeof(TypeDescriptor const *));
                            return_types[0] = return_type;
                            return_count = 1;
                        }
                    }
                }
                else if (sig_child->type == AST_NODE_PARAMETER_LIST)
                {
                    param_list_node = sig_child;
                }
            }
        }
    }

    // Extract param type descriptors from the parameter list
    if (param_list_node != NULL && param_list_node->list.count > 0)
    {
        odin_grammar_node_t * params = param_list_node->list.children[0];
        if (params != NULL && params->type == AST_NODE_PARAMETERS)
        {
            for (size_t k = 0; k < params->list.count; k++)
            {
                odin_grammar_node_t * param = params->list.children[k];
                if (param == NULL || param->type != AST_NODE_PARAMETER)
                    continue;

                odin_grammar_node_t * param_ident = NULL;
                odin_grammar_node_t * param_type_node = NULL;
                for (size_t ci = 0; ci < param->list.count; ci++)
                {
                    odin_grammar_node_t * child = param->list.children[ci];
                    if (child == NULL)
                        continue;
                    if (child->type == AST_NODE_IDENTIFIER && param_ident == NULL)
                        param_ident = child;
                    else if (child->type == AST_NODE_IDENTIFIER || is_type_node(child))
                        param_type_node = child;
                }
                if (param_type_node == NULL)
                {
                    for (size_t ci = param->list.count; ci > 0; ci--)
                    {
                        odin_grammar_node_t * child = param->list.children[ci - 1];
                        if (child == NULL)
                            continue;
                        if (child->type == AST_NODE_IDENTIFIER && child != param_ident)
                        {
                            param_type_node = child;
                            break;
                        }
                    }
                }
                if (param_ident == NULL || param_type_node == NULL)
                    continue;

                TypeDescriptor const * pt = sem_resolve_type_expr(ctx, param_type_node);
                if (pt == NULL)
                    continue;
                param_type_node->resolved_type = (TypeDescriptor *)pt;

                // If this is a variadic .. parameter, wrap the type in a slice
                bool is_variadic_param = false;
                for (size_t ci = 0; ci < param->list.count; ci++)
                {
                    if (param->list.children[ci] != NULL
                        && param->list.children[ci]->type == AST_NODE_ELLIPSIS)
                    {
                        is_variadic_param = true;
                        break;
                    }
                }
                if (is_variadic_param && pt != NULL)
                {
                    pt = get_or_create_slice_type(ctx->type_registry, pt);
                    if (pt == NULL)
                        continue;
                    param_type_node->resolved_type = (TypeDescriptor *)pt;
                    is_variadic = true;
                }

                if (param_count >= (int)param_cap)
                {
                    size_t new_cap = param_cap == 0 ? 4 : param_cap * 2;
                    TypeDescriptor const ** tmp = realloc(param_types, new_cap * sizeof(TypeDescriptor const *));
                    if (tmp == NULL)
                    {
                        free(param_types);
                        return NULL;
                    }
                    param_types = tmp;
                    param_cap = new_cap;
                }
                param_types[param_count++] = pt;
            }
        }
    }

    // Also detect bare ... variadic
    if (!is_variadic && param_list_node != NULL && param_list_node->list.count > 0)
    {
        odin_grammar_node_t * params = param_list_node->list.children[0];
        if (params != NULL && params->type == AST_NODE_PARAMETERS)
        {
            for (size_t k = 0; k < params->list.count; k++)
            {
                odin_grammar_node_t * p = params->list.children[k];
                if (p == NULL)
                    continue;
                if (p->type == AST_NODE_ELLIPSIS)
                {
                    is_variadic = true;
                    break;
                }
            }
        }
    }

    // Create the procedure type descriptor
    if (return_count == 0)
        return_types = NULL;

    TypeDescriptor const * proc_type = get_or_create_proc_type(
        ctx->type_registry, return_type, param_types, param_count, return_types, return_count, is_variadic, cc
    );

    if (out_param_types) *out_param_types = param_types; else free(param_types);
    if (out_param_count) *out_param_count = param_count;
    if (out_return_type) *out_return_type = return_type;
    if (out_return_count) *out_return_count = return_count;
    if (out_cc) *out_cc = cc;
    if (out_is_variadic) *out_is_variadic = is_variadic;

    if (return_types && out_return_count == NULL)
        free((void *)return_types);

    node->resolved_type = (TypeDescriptor *)proc_type;
    return proc_type;
}

static void
sem_analyse_procedure_literal(SemContext * ctx, odin_grammar_node_t * node, char const * proc_name)
{
    TypeDescriptor const * return_type = NULL;
    TypeDescriptor const ** return_types = NULL;
    int return_count = 0;
    int param_count = 0;
    TypeDescriptor const ** param_types = NULL;
    calling_convention_t cc = CALLING_CONV_ODIN;
    bool is_variadic = false;

    odin_grammar_node_t * comp_stmt_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child && child->type == AST_NODE_COMPOUND_STATEMENT)
        {
            comp_stmt_node = child;
            break;
        }
    }

    // Resolve signature (param types, return types, calling convention)
    TypeDescriptor const * proc_type = sem_resolve_procedure_signature(
        ctx, node, &param_types, &param_count, &return_type, &return_count, &cc, &is_variadic
    );
    node->resolved_type = (TypeDescriptor *)proc_type;

    // Push a new scope, register parameters, analyse body
    generator_push_scope(ctx->gen_ctx);

    // Register implicit 'context' variable in every procedure scope
    {
        TypeDescriptor const * ctx_type = type_descriptor_get_context_type(ctx->type_registry);
        if (ctx_type)
        {
            TypedValue ctx_tv = create_typed_value(NULL, ctx_type, true);
            generator_add_symbol(ctx->gen_ctx, "context", ctx_tv);
        }
    }

    // Register parameters in the body scope (types already resolved by sem_resolve_procedure_signature)
    {
        odin_grammar_node_t * param_list_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && child->type == AST_NODE_PROCEDURE_SIGNATURE)
            {
                for (size_t j = 0; j < child->list.count; j++)
                {
                    odin_grammar_node_t * sig_child = child->list.children[j];
                    if (sig_child && sig_child->type == AST_NODE_PARAMETER_LIST)
                    {
                        param_list_node = sig_child;
                        break;
                    }
                }
                break;
            }
        }

        if (param_list_node != NULL && param_list_node->list.count > 0)
        {
            odin_grammar_node_t * params = param_list_node->list.children[0];
            if (params != NULL && params->type == AST_NODE_PARAMETERS)
            {
                for (size_t k = 0; k < params->list.count; k++)
                {
                    odin_grammar_node_t * param = params->list.children[k];
                    if (param == NULL || param->type != AST_NODE_PARAMETER)
                        continue;

                    odin_grammar_node_t * param_ident = NULL;
                    odin_grammar_node_t * param_type_node = NULL;
                    for (size_t ci = 0; ci < param->list.count; ci++)
                    {
                        odin_grammar_node_t * pc = param->list.children[ci];
                        if (pc == NULL)
                            continue;
                        if (pc->type == AST_NODE_IDENTIFIER && param_ident == NULL)
                            param_ident = pc;
                        else if (pc->type == AST_NODE_IDENTIFIER || is_type_node(pc))
                            param_type_node = pc;
                    }
                    if (param_type_node == NULL)
                    {
                        for (size_t ci = param->list.count; ci > 0; ci--)
                        {
                            odin_grammar_node_t * pc = param->list.children[ci - 1];
                            if (pc == NULL)
                                continue;
                            if (pc->type == AST_NODE_IDENTIFIER && pc != param_ident)
                            {
                                param_type_node = pc;
                                break;
                            }
                        }
                    }
                    if (param_ident == NULL || param_type_node == NULL)
                        continue;

                    TypeDescriptor const * param_type = param_type_node->resolved_type;
                    if (param_type == NULL)
                        continue;

                    TypedValue tv = create_typed_value(NULL, param_type, true);
                    generator_add_symbol(ctx->gen_ctx, param_ident->text, tv);
                }
            }
        }
    }

    // Pre-register procedure name in body scope for recursion
    if (proc_name != NULL && node->resolved_type != NULL)
    {
        TypedValue tv = create_typed_value(NULL, node->resolved_type, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), proc_name, tv);
    }
    // Also update the parent scope with the resolved type (forward reference support)
    if (proc_name != NULL && node->resolved_type != NULL)
    {
        scope_t * parent = generator_current_scope(ctx->gen_ctx)->parent;
        if (parent)
        {
            TypedValue tv = create_typed_value(NULL, node->resolved_type, false);
            scope_add_symbol(parent, proc_name, tv);
        }
    }

    if (comp_stmt_node)
    {
        TypeDescriptor const * expected_ret = node->resolved_type
            ? node->resolved_type : type_descriptor_get_void_type(ctx->type_registry);
        sem_analyse_compound_statement(ctx, comp_stmt_node, expected_ret);
    }

    generator_pop_scope(ctx->gen_ctx);
}

// --- Top-level analysis ---

static void
sem_set_symbol_private(scope_t * scope, char const * name, bool is_private)
{
    if (name == NULL || !is_private)
        return;
    symbol_t * sym = scope_find_symbol_entry(scope, name);
    if (sym)
        sym->is_private = true;
}

static void
sem_register_top_level_declaration(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->list.count < 2)
        return;

    sem_analyse_attributes(node);

    bool is_private = false;
    if (node->metadata)
    {
        ProcDeclAttributes * attrs = (ProcDeclAttributes *)node->metadata;
        is_private = attrs->is_private;
    }

    odin_grammar_node_t * name_node = node_find_child(node, AST_NODE_IDENTIFIER);
    if (name_node == NULL)
        name_node = node->list.children[0];

    // Find the value node to resolve procedure types in pass 1
    odin_grammar_node_t * value_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && child != name_node && child->type != AST_NODE_ATTRIBUTE)
        {
            value_node = child;
            break;
        }
    }

        TypeDescriptor const * resolved_type = NULL;
        if (value_node != NULL && value_node->type == AST_NODE_PROCEDURE_DEFINITION)
        {
            resolved_type = sem_resolve_procedure_signature(ctx, value_node, NULL, NULL, NULL, NULL, NULL, NULL);
        }
        else if (value_node != NULL && value_node->type == AST_NODE_PROC_OVERLOAD_BUNDLE)
        {
            int candidate_count = (int)value_node->list.count;
            if (candidate_count > 0)
            {
                TypeDescriptor const ** candidate_types = (TypeDescriptor const **)malloc(
                    (size_t)candidate_count * sizeof(TypeDescriptor const *)
                );
                symbol_t ** candidate_symbols = (symbol_t **)malloc(
                    (size_t)candidate_count * sizeof(symbol_t *)
                );
                int valid_count = 0;
                for (int i = 0; i < candidate_count; i++)
                {
                    odin_grammar_node_t * id_node = value_node->list.children[i];
                    if (id_node == NULL || id_node->type != AST_NODE_IDENTIFIER || id_node->text == NULL)
                        continue;
                    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), id_node->text);
                    if (sym && sym->value.type_info && sym->value.type_info->kind == TD_KIND_PROC)
                    {
                        candidate_types[valid_count] = sym->value.type_info;
                        candidate_symbols[valid_count] = sym;
                        valid_count++;
                    }
                    else
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "candidate '%s' in overload bundle is not a procedure", id_node->text);
                        sem_error_list_add(&ctx->errors, NULL, id_node, buf);
                    }
                }
                if (valid_count > 0)
                {
                    resolved_type = get_or_create_overload_bundle_type(
                        ctx->type_registry, candidate_types, candidate_symbols, valid_count
                    );
                    value_node->resolved_type = (TypeDescriptor *)resolved_type;
                }
                free(candidate_types);
                free(candidate_symbols);
            }
        }

    if (name_node->type == AST_NODE_IDENTIFIER)
    {
        TypedValue tv = create_typed_value(NULL, resolved_type, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
        sem_set_symbol_private(generator_current_scope(ctx->gen_ctx), name_node->text, is_private);

        // Try to evaluate as a compile-time integer constant
        if (value_node != NULL && value_node->type != AST_NODE_PROCEDURE_DEFINITION && value_node->type != AST_NODE_PROC_OVERLOAD_BUNDLE)
        {
            int const_ok = 0;
            long long const_val = sem_evaluate_constant_int(ctx, value_node, &const_ok);
            if (const_ok)
            {
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (sym)
                {
                    sym->const_int_val = const_val;
                    sym->has_const_int_val = true;
                }
            }
        }
    }
    else if (name_node->type == AST_NODE_IDENTIFIER_LIST)
    {
        for (size_t i = 0; i < name_node->list.count; i++)
        {
            odin_grammar_node_t * id = name_node->list.children[i];
            if (id == NULL || id->type != AST_NODE_IDENTIFIER)
                continue;
            TypedValue tv = create_typed_value(NULL, resolved_type, false);
            scope_add_symbol(generator_current_scope(ctx->gen_ctx), id->text, tv);
            sem_set_symbol_private(generator_current_scope(ctx->gen_ctx), id->text, is_private);
        }
    }
}

static void
sem_register_top_level_variable(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->list.count < 1)
        return;

    odin_grammar_node_t * id_list = node->list.children[0];
    if (id_list->type != AST_NODE_IDENTIFIER_LIST)
        return;

    for (size_t i = 0; i < id_list->list.count; i++)
    {
        odin_grammar_node_t * name_node = id_list->list.children[i];
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
            continue;
        TypedValue tv = create_typed_value(NULL, NULL, true);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
    }
}

static void
import_using_copy_symbol(void * value, void * user_data)
{
    symbol_t * sym = (symbol_t *)value;
    scope_t * target_scope = (scope_t *)user_data;
    if (sym == NULL || sym->name == NULL || target_scope == NULL || sym->is_private)
        return;
    scope_add_symbol(target_scope, sym->name, sym->value);
    if (sym->has_const_int_val)
    {
        symbol_t * copy = scope_find_symbol_entry(target_scope, sym->name);
        if (copy)
        {
            copy->const_int_val = sym->const_int_val;
            copy->has_const_int_val = true;
        }
    }
}

static bool
import_push_path(SemContext * ctx, char const * resolved_path)
{
    if (resolved_path == NULL)
        return false;
    for (int i = 0; i < ctx->import_stack_count; i++)
    {
        if (strcmp(ctx->import_stack[i], resolved_path) == 0)
        {
            fprintf(stderr, "Error: Import cycle detected — '%s'\n", resolved_path);
            return false;
        }
    }
    if (ctx->import_stack_count >= ctx->import_stack_capacity)
    {
        int new_cap = ctx->import_stack_capacity == 0 ? 8 : ctx->import_stack_capacity * 2;
        char ** new_arr = realloc(ctx->import_stack, (size_t)new_cap * sizeof(char *));
        if (new_arr == NULL)
            return false;
        ctx->import_stack = new_arr;
        ctx->import_stack_capacity = new_cap;
    }
    ctx->import_stack[ctx->import_stack_count++] = strdup(resolved_path);
    return true;
}

static void
import_pop_path(SemContext * ctx)
{
    if (ctx->import_stack_count > 0)
    {
        ctx->import_stack_count--;
        free(ctx->import_stack[ctx->import_stack_count]);
        ctx->import_stack[ctx->import_stack_count] = NULL;
    }
}

static void
sem_analyse_attributes(odin_grammar_node_t * decl_node)
{
    if (decl_node == NULL || decl_node->list.count < 3)
        return;
    odin_grammar_node_t * first = decl_node->list.children[0];
    if (first == NULL || first->type != AST_NODE_ATTRIBUTE)
        return;

    odin_grammar_node_t * attr_list = NULL;
    for (size_t i = 0; i < first->list.count; i++)
    {
        if (first->list.children[i] && first->list.children[i]->type == AST_NODE_ATTR_LIST)
        {
            attr_list = first->list.children[i];
            break;
        }
    }
    if (attr_list == NULL)
        return;

    ProcDeclAttributes * attrs = calloc(1, sizeof(ProcDeclAttributes));
    for (size_t i = 0; i < attr_list->list.count; i++)
    {
        odin_grammar_node_t * item = attr_list->list.children[i];
        if (item == NULL || item->type != AST_NODE_ATTR_ITEM)
            continue;
        odin_grammar_node_t * name_node = NULL;
        odin_grammar_node_t * value_node = NULL;
        for (size_t j = 0; j < item->list.count; j++)
        {
            odin_grammar_node_t * child = item->list.children[j];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_IDENTIFIER)
                name_node = child;
            else
                value_node = child;
        }
        if (name_node == NULL || name_node->text == NULL)
            continue;

        if (strcmp(name_node->text, "link_name") == 0 && value_node != NULL && value_node->text)
        {
            size_t len = strlen(value_node->text);
            if (len >= 2 && (value_node->text[0] == '"' || value_node->text[0] == '`'))
            {
                attrs->link_name = strndup(value_node->text + 1, len - 2);
            }
        }
        else if (strcmp(name_node->text, "require_results") == 0)
        {
            attrs->require_results = true;
        }
        else if (strcmp(name_node->text, "private") == 0)
        {
            attrs->is_private = true;
        }
        else if (strcmp(name_node->text, "builtin") == 0)
        {
            attrs->is_builtin = true;
        }
    }
    decl_node->metadata = attrs;
}

static bool
has_odin_extension(char const * path)
{
    if (path == NULL) return false;
    size_t len = strlen(path);
    return (len > 5 && strcmp(path + len - 5, ".odin") == 0);
}

// Parse an imported path, which may be a single .odin file or a directory of .odin files.
// Dispatches to parse_imported_file or parse_imported_directory as appropriate.
static ImportedPackage *
parse_imported_path(char const * path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks)
{
    if (has_odin_extension(path))
        return parse_imported_file(path, parser, hooks);
    else
        return parse_imported_directory(path, parser, hooks);
}

static void
sem_pass1_register_top_level_ex(SemContext * ctx, odin_grammar_node_t * program_ast)
{
    if (program_ast == NULL)
        return;

    // Auto-import core:runtime as an implicit using import (prelude)
    // Check if runtime is already imported by checking all existing imports
    bool runtime_already_imported = false;
    for (int ri = 0; ri < ctx->import_count; ri++)
    {
        if (ctx->imports[ri] && ctx->imports[ri]->is_runtime)
        {
            runtime_already_imported = true;
            break;
        }
    }
    if (!runtime_already_imported)
    {
        char * runtime_path = resolve_import_path("core:runtime", ctx->source_dir, ctx->odin_root);
        if (runtime_path)
        {
            ImportedPackage * rp = parse_imported_path(runtime_path, ctx->parser, ctx->hook_registry);
            if (rp)
            {
                rp->is_runtime = true; // mark so we don't re-import
                rp->is_using = true;
                rp->package_name = strdup("runtime");
                rp->analysed = true;

                scope_t * rp_scope = scope_create(NULL, ctx->gen_ctx->context, ctx->gen_ctx->builder);
                rp->package_scope = rp_scope;

                int saved_count = ctx->gen_ctx->count;
                ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = rp_scope;

                char * saved_pkg_name = ctx->package_name;
                ctx->package_name = NULL;
                char const * saved_file_path = ctx->source_file_path;
                ctx->source_file_path = rp->source_path;

                // Add to imports array BEFORE recursive analysis to prevent re-entry
                if (ctx->import_count >= ctx->import_capacity)
                {
                    int new_cap = ctx->import_capacity == 0 ? 8 : ctx->import_capacity * 2;
                    ImportedPackage ** new_arr = realloc(ctx->imports, (size_t)new_cap * sizeof(ImportedPackage *));
                    if (new_arr == NULL)
                    {
                        perror("realloc");
                        exit(1);
                    }
                    ctx->imports = new_arr;
                    ctx->import_capacity = new_cap;
                }
                ctx->imports[ctx->import_count++] = rp;

                sem_pass1_register_top_level_ex(ctx, rp->ast);
                sem_pass2_analyse_bodies_ast(ctx, rp->ast);

                if (rp->package_name == NULL && ctx->package_name != NULL)
                {
                    free(rp->package_name);
                    rp->package_name = strdup(ctx->package_name);
                }

                ctx->package_name = saved_pkg_name;
                ctx->source_file_path = saved_file_path;
                ctx->gen_ctx->count = saved_count;
            }
            free(runtime_path);
        }
    }

    // Copy runtime symbols into the current scope (skip if we are core:runtime itself)
    for (int ri = 0; ri < ctx->import_count; ri++)
    {
        if (ctx->imports[ri] && ctx->imports[ri]->is_runtime && ctx->imports[ri]->package_scope)
        {
            scope_t * cur = generator_current_scope(ctx->gen_ctx);
            if (cur != ctx->imports[ri]->package_scope)
            {
                generic_hash_table_iterate(
                    ctx->imports[ri]->package_scope->symbols.by_name,
                    import_using_copy_symbol,
                    cur
                );
            }
            break;
        }
    }

    for (size_t i = 0; i < program_ast->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = program_ast->list.children[i];
        if (ext_decl == NULL)
            continue;

        if (ext_decl->type == AST_NODE_EXTERNAL_DECLARATIONS)
        {
            for (size_t j = 0; j < ext_decl->list.count; j++)
            {
                odin_grammar_node_t * top_decl = ext_decl->list.children[j];
                if (top_decl == NULL)
                    continue;

                if (top_decl->type == AST_NODE_PACKAGE_CLAUSE)
                {
                    // PackageClause children: [Identifier("name")]
                    if (top_decl->list.count > 0 && top_decl->list.children[0] && top_decl->list.children[0]->text)
                    {
                        free(ctx->package_name);
                        ctx->package_name = strdup(top_decl->list.children[0]->text);
                    }
                }
                else if (top_decl->type == AST_NODE_IMPORT)
                {
                    // ImportSimple children: [StringLiteral("path")]
                    if (top_decl->list.count < 1 || top_decl->list.children[0] == NULL)
                        continue;
                    odin_grammar_node_t * path_node = top_decl->list.children[0];
                    char * import_path = strip_quotes(path_node->text);
                    if (import_path == NULL)
                        continue;

                    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
                    free(import_path);
                    if (resolved == NULL)
                    {
                        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
                        continue;
                    }

                    if (!import_push_path(ctx, resolved))
                    {
                        free(resolved);
                        continue;
                    }

                    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);

                    // import_push_path strdup'd resolved; stack owns it now
                    if (pkg == NULL)
                    {
                        import_pop_path(ctx);
                        free(resolved);
                        continue;
                    }

                    if (ctx->import_count >= ctx->import_capacity)
                    {
                        int new_cap = ctx->import_capacity == 0 ? 8 : ctx->import_capacity * 2;
                        ImportedPackage ** new_arr = realloc(ctx->imports, (size_t)new_cap * sizeof(ImportedPackage *));
                        if (new_arr == NULL)
                        {
                            import_pop_path(ctx);
                            free(resolved);
                            imported_package_free(pkg);
                            continue;
                        }
                        ctx->imports = new_arr;
                        ctx->import_capacity = new_cap;
                    }
                    ctx->imports[ctx->import_count++] = pkg;

                    if (!pkg->analysed)
                    {
                        scope_t * pkg_scope = scope_create(NULL, ctx->gen_ctx->context, ctx->gen_ctx->builder);
                        pkg->package_scope = pkg_scope;

                        int saved_count = ctx->gen_ctx->count;
                        ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = pkg_scope;

                        char * saved_pkg_name = ctx->package_name;
                        ctx->package_name = NULL;

                        char const * saved_file_path = ctx->source_file_path;
                        ctx->source_file_path = pkg->source_path;

                        pkg->analysed = true;
                        sem_pass1_register_top_level_ex(ctx, pkg->ast);

                        sem_pass2_analyse_bodies_ast(ctx, pkg->ast);

                        if (pkg->package_name == NULL && ctx->package_name != NULL)
                            pkg->package_name = strdup(ctx->package_name);

                        ctx->package_name = saved_pkg_name;
                        ctx->source_file_path = saved_file_path;

                        ctx->gen_ctx->count = saved_count;
                    }

                    import_pop_path(ctx);
                    free(resolved);
                }
                else if (top_decl->type == AST_NODE_IMPORT_NAMED)
                {
                    if (top_decl->list.count < 2 || top_decl->list.children[0] == NULL
                        || top_decl->list.children[0]->text == NULL || top_decl->list.children[1] == NULL)
                        continue;
                    char const * alias_name = top_decl->list.children[0]->text;
                    odin_grammar_node_t * path_node = top_decl->list.children[1];
                    char * import_path = strip_quotes(path_node->text);
                    if (import_path == NULL)
                        continue;

                    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
                    free(import_path);
                    if (resolved == NULL)
                    {
                        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
                        continue;
                    }

                    if (!import_push_path(ctx, resolved))
                    {
                        free(resolved);
                        continue;
                    }

                    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);
                    if (pkg == NULL)
                    {
                        import_pop_path(ctx);
                        free(resolved);
                        continue;
                    }

                    if (ctx->import_count >= ctx->import_capacity)
                    {
                        int new_cap = ctx->import_capacity == 0 ? 8 : ctx->import_capacity * 2;
                        ImportedPackage ** new_arr = realloc(ctx->imports, (size_t)new_cap * sizeof(ImportedPackage *));
                        if (new_arr == NULL)
                        {
                            import_pop_path(ctx);
                            free(resolved);
                            imported_package_free(pkg);
                            continue;
                        }
                        ctx->imports = new_arr;
                        ctx->import_capacity = new_cap;
                    }
                    ctx->imports[ctx->import_count++] = pkg;

                    if (!pkg->analysed)
                    {
                        scope_t * pkg_scope = scope_create(NULL, ctx->gen_ctx->context, ctx->gen_ctx->builder);
                        pkg->package_scope = pkg_scope;

                        int saved_count = ctx->gen_ctx->count;
                        ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = pkg_scope;

                        char * saved_pkg_name = ctx->package_name;
                        ctx->package_name = NULL;

                        char const * saved_file_path = ctx->source_file_path;
                        ctx->source_file_path = pkg->source_path;

                        pkg->analysed = true;
                        sem_pass1_register_top_level_ex(ctx, pkg->ast);

                        sem_pass2_analyse_bodies_ast(ctx, pkg->ast);

                        if (pkg->package_name == NULL && ctx->package_name != NULL)
                            pkg->package_name = strdup(ctx->package_name);

                        free(pkg->package_name);
                        pkg->package_name = strdup(alias_name);

                        ctx->package_name = saved_pkg_name;
                        ctx->source_file_path = saved_file_path;

                        ctx->gen_ctx->count = saved_count;
                    }

                    import_pop_path(ctx);
                    free(resolved);
                }
                else if (top_decl->type == AST_NODE_IMPORT_USING)
                {
                    if (top_decl->list.count < 1 || top_decl->list.children[0] == NULL)
                        continue;
                    odin_grammar_node_t * path_node = top_decl->list.children[0];
                    char * import_path = strip_quotes(path_node->text);
                    if (import_path == NULL)
                        continue;

                    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
                    free(import_path);
                    if (resolved == NULL)
                    {
                        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
                        continue;
                    }

                    if (!import_push_path(ctx, resolved))
                    {
                        free(resolved);
                        continue;
                    }

                    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);
                    if (pkg == NULL)
                    {
                        import_pop_path(ctx);
                        free(resolved);
                        continue;
                    }

                    if (ctx->import_count >= ctx->import_capacity)
                    {
                        int new_cap = ctx->import_capacity == 0 ? 8 : ctx->import_capacity * 2;
                        ImportedPackage ** new_arr = realloc(ctx->imports, (size_t)new_cap * sizeof(ImportedPackage *));
                        if (new_arr == NULL)
                        {
                            import_pop_path(ctx);
                            free(resolved);
                            imported_package_free(pkg);
                            continue;
                        }
                        ctx->imports = new_arr;
                        ctx->import_capacity = new_cap;
                    }
                    ctx->imports[ctx->import_count++] = pkg;
                    pkg->is_using = true;

                    if (!pkg->analysed)
                    {
                        scope_t * pkg_scope = scope_create(NULL, ctx->gen_ctx->context, ctx->gen_ctx->builder);
                        pkg->package_scope = pkg_scope;

                        int saved_count = ctx->gen_ctx->count;
                        ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = pkg_scope;

                        char * saved_pkg_name = ctx->package_name;
                        ctx->package_name = NULL;

                        char const * saved_file_path = ctx->source_file_path;
                        ctx->source_file_path = pkg->source_path;

                        pkg->analysed = true;
                        sem_pass1_register_top_level_ex(ctx, pkg->ast);
                        sem_pass2_analyse_bodies_ast(ctx, pkg->ast);

                        if (pkg->package_name == NULL && ctx->package_name != NULL)
                            pkg->package_name = strdup(ctx->package_name);

                        ctx->package_name = saved_pkg_name;
                        ctx->source_file_path = saved_file_path;

                        ctx->gen_ctx->count = saved_count;

                        scope_t * current_scope = generator_current_scope(ctx->gen_ctx);
                        generic_hash_table_iterate(pkg_scope->symbols.by_name, import_using_copy_symbol, current_scope);
                    }

                    import_pop_path(ctx);
                    free(resolved);
                }
                else if (top_decl->type == AST_NODE_CONSTANT_DECL)
                {
                    sem_register_top_level_declaration(ctx, top_decl);
                }
                else if (top_decl->type == AST_NODE_VARIABLE_DECL)
                {
                    sem_register_top_level_variable(ctx, top_decl);
                }
                else if (top_decl->type == AST_NODE_FOREIGN_IMPORT)
                {
                }
                else if (top_decl->type == AST_NODE_FOREIGN_BLOCK)
                {
                    for (size_t k = 0; k < top_decl->list.count; k++)
                    {
                        odin_grammar_node_t * fb_child = top_decl->list.children[k];
                        if (fb_child == NULL)
                            continue;
                        if (fb_child->type == AST_NODE_CONSTANT_DECL)
                            sem_register_top_level_declaration(ctx, fb_child);
                    }
                }
                else if (top_decl->type == AST_NODE_USING_DECL)
                {
                    for (size_t k = 0; k < top_decl->list.count; k++)
                    {
                        odin_grammar_node_t * inner = top_decl->list.children[k];
                        if (inner == NULL)
                            continue;
                        if (inner->type == AST_NODE_VARIABLE_DECL)
                            sem_register_top_level_variable(ctx, inner);
                        else if (inner->type == AST_NODE_CONSTANT_DECL)
                            sem_register_top_level_declaration(ctx, inner);
                    }
                }
                else if (top_decl->type == AST_NODE_DIRECTIVE_WITH_ARGS)
                {
                    if (top_decl->text && strncmp(top_decl->text, "#assert", 7) == 0)
                    {
                        for (size_t k = 0; k < top_decl->list.count; k++)
                        {
                            odin_grammar_node_t * ac = top_decl->list.children[k];
                            if (ac == NULL || ac->type == AST_NODE_IDENTIFIER)
                                continue;
                            sem_evaluate_expr(ctx, ac);
                            if (ac->resolved_type == NULL)
                                break;
                            int result = sem_evaluate_constant_bool(ctx, ac);
                            if (result == 0)
                                sem_error_list_add(&ctx->errors, NULL, top_decl, "#assert failed");
                            break;
                        }
                    }
                }
                else if (top_decl->type == AST_NODE_WHEN_DECL)
                {
                    size_t k = 0;
                    bool matched = false;
                    while (k < top_decl->list.count)
                    {
                        odin_grammar_node_t * wc = top_decl->list.children[k];
                        if (wc == NULL)
                        {
                            k++;
                            continue;
                        }
                        if (wc->type == AST_NODE_WHEN_BODY)
                        {
                            if (!matched)
                            {
                                for (size_t m = 0; m < wc->list.count; m++)
                                {
                                    odin_grammar_node_t * inner = wc->list.children[m];
                                    if (inner == NULL)
                                        continue;
                                    if (inner->type == AST_NODE_CONSTANT_DECL)
                                        sem_register_top_level_declaration(ctx, inner);
                                    else if (inner->type == AST_NODE_VARIABLE_DECL)
                                        sem_register_top_level_variable(ctx, inner);
                                }
                            }
                            break;
                        }
                        int cond = sem_evaluate_constant_bool(ctx, wc);
                        k++;
                        if (cond == 1 && !matched)
                        {
                            matched = true;
                            if (k < top_decl->list.count)
                            {
                                odin_grammar_node_t * body = top_decl->list.children[k];
                                if (body && body->type == AST_NODE_WHEN_BODY)
                                {
                                    for (size_t m = 0; m < body->list.count; m++)
                                    {
                                        odin_grammar_node_t * inner = body->list.children[m];
                                        if (inner == NULL)
                                            continue;
                                        if (inner->type == AST_NODE_CONSTANT_DECL)
                                            sem_register_top_level_declaration(ctx, inner);
                                        else if (inner->type == AST_NODE_VARIABLE_DECL)
                                            sem_register_top_level_variable(ctx, inner);
                                    }
                                }
                            }
                        }
                        k++;
                    }
                }
            }
        }
    }
}

static void
sem_pass1_register_top_level(SemContext * ctx)
{
    sem_pass1_register_top_level_ex(ctx, ctx->ast);
}

// --- Pass 2: body analysis dispatcher ---

static void
sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type)
{
    if (node == NULL)
        return;

    switch (node->type)
    {
    case AST_NODE_RETURN_STATEMENT:
        sem_analyse_return_statement(ctx, node, expected_return_type);
        break;

    case AST_NODE_COMPOUND_STATEMENT:
        generator_push_scope(ctx->gen_ctx);
        sem_analyse_compound_statement(ctx, node, expected_return_type);
        generator_pop_scope(ctx->gen_ctx);
        break;

    case AST_NODE_EXPRESSION_STATEMENT:
        if (node->list.count > 0)
        {
            sem_evaluate_expr(ctx, node->list.children[0]);
        }
        break;

    case AST_NODE_ASSIGN_STATEMENT:
    {
        // Find the assign operator to split LHS / RHS
        odin_grammar_node_t * op_node = node_find_op(node);
        size_t rhs_idx = node->list.count;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (node->list.children[i] == op_node && i + 1 < node->list.count)
            {
                rhs_idx = i + 1;
                break;
            }
        }
        for (size_t i = 0; i < node->list.count; i++)
        {
            sem_evaluate_expr(ctx, node->list.children[i]);
        }
        // Type check: LHS[0] type vs RHS type (only for simple single-LHS assignments)
        if (rhs_idx < node->list.count && op_node != NULL)
        {
            odin_grammar_node_t * rhs_node = node->list.children[rhs_idx];
            // Get LHS type (first child, skipping the operator)
            TypeDescriptor const * lhs_type = NULL;
            odin_grammar_node_t * lhs_node = NULL;
            for (size_t i = 0; i < rhs_idx; i++)
            {
                if (node->list.children[i] != NULL && node->list.children[i] != op_node)
                {
                    lhs_type = node->list.children[i]->resolved_type;
                    lhs_node = node->list.children[i];
                    break;
                }
            }
            // Unwrap expression wrappers to find the innermost node with a resolved_type
            odin_grammar_node_t * lhs_inner = lhs_node;
            while (lhs_inner != NULL && lhs_inner->list.count == 1 && lhs_inner->list.children[0] != NULL
                   && (lhs_inner->type == AST_NODE_EXPRESSION || lhs_inner->type == AST_NODE_PRIMARY_EXPRESSION
                       || lhs_inner->type == AST_NODE_POSTFIX_EXPRESSION))
            {
                lhs_inner = lhs_inner->list.children[0];
                if (lhs_inner->resolved_type != NULL)
                    lhs_type = lhs_inner->resolved_type;
            }
            TypeDescriptor const * rhs_type = rhs_node ? rhs_node->resolved_type : NULL;
            if (lhs_type != NULL && rhs_type != NULL)
            {
                sem_check_assignment(ctx, lhs_node, lhs_type, rhs_type, rhs_node);
            }
        }
        break;
    }

    case AST_NODE_VARIABLE_DECL:
    {
        if (node->list.count < 1)
            break;
        odin_grammar_node_t * id_list = node->list.children[0];
        if (id_list == NULL || id_list->type != AST_NODE_IDENTIFIER_LIST)
            break;

        size_t id_count = id_list->list.count;
        TypeDescriptor const * var_type = NULL;
        odin_grammar_node_t * type_node = NULL;
        odin_grammar_node_t * init_node = NULL;

        for (size_t i = 1; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
                type_node = child;
            else
                init_node = child;
        }
        if (type_node == NULL && init_node == NULL)
        {
            for (size_t i = 1; i < node->list.count; i++)
            {
                odin_grammar_node_t * child = node->list.children[i];
                if (child != NULL && child != id_list)
                {
                    init_node = child;
                    break;
                }
            }
        }

        if (type_node)
        {
            var_type = sem_resolve_type_expr(ctx, type_node);
            if (type_node)
                type_node->resolved_type = (TypeDescriptor *)var_type;
        }

        if (init_node)
        {
            TypeDescriptor const * init_type = sem_evaluate_expr(ctx, init_node);
            if (type_node == NULL)
            {
                var_type = init_type;
            }
            else if (var_type != NULL && init_type != NULL)
            {
                // Check init type is compatible with declared variable type
                sem_check_assignment(ctx, node, var_type, init_type, init_node);
            }

            // Tuple destructuring: a, b := some_tuple
            if (id_count > 1 && init_type != NULL && init_type->kind == TD_KIND_TUPLE)
            {
                for (size_t vi = 0; vi < id_count && vi < (size_t)init_type->as.tuple.element_count; vi++)
                {
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                        continue;
                    TypedValue tv = create_typed_value(NULL, init_type->as.tuple.element_types[vi], true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
                break;
            }

            // Multi-return destructuring: a, b := foo()
            if (id_count > 1 && init_type != NULL && init_type->kind == TD_KIND_PROC)
            {
                ProcMetadata const * pm = &init_type->proc_metadata;
                for (size_t vi = 0; vi < id_count && vi < (size_t)pm->return_count; vi++)
                {
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                        continue;
                    TypedValue tv = create_typed_value(NULL, pm->returns[vi], true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
                break;
            }
        }

        if (var_type)
        {
            node->resolved_type = (TypeDescriptor *)var_type;
            if (id_count == 1)
            {
                odin_grammar_node_t * name_node = id_list->list.children[0];
                if (name_node && name_node->type == AST_NODE_IDENTIFIER)
                {
                    TypedValue tv = create_typed_value(NULL, var_type, true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
            }
        }
        break;
    }

    case AST_NODE_CONSTANT_DECL:
    {
        if (node->list.count < 2)
            break;
        sem_analyse_attributes(node);

        odin_grammar_node_t * name_node = node_find_child(node, AST_NODE_IDENTIFIER);
        odin_grammar_node_t * value_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child != NULL && child != name_node && child->type != AST_NODE_ATTRIBUTE)
            {
                value_node = child;
                break;
            }
        }
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
            break;
        if (value_node == NULL)
            break;

        if (value_node->type == AST_NODE_PROCEDURE_DEFINITION)
        {
            sem_analyse_procedure_literal(ctx, value_node, name_node->text);
        }
        else if (value_node->type == AST_NODE_PROC_OVERLOAD_BUNDLE)
        {
            int candidate_count = (int)value_node->list.count;
            if (candidate_count > 0)
            {
                TypeDescriptor const ** candidate_types = (TypeDescriptor const **)malloc(
                    (size_t)candidate_count * sizeof(TypeDescriptor const *)
                );
                symbol_t ** candidate_symbols = (symbol_t **)malloc(
                    (size_t)candidate_count * sizeof(symbol_t *)
                );
                int valid_count = 0;
                for (int i = 0; i < candidate_count; i++)
                {
                    odin_grammar_node_t * id_node = value_node->list.children[i];
                    if (id_node == NULL || id_node->type != AST_NODE_IDENTIFIER || id_node->text == NULL)
                        continue;
                    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), id_node->text);
                    if (sym && sym->value.type_info && sym->value.type_info->kind == TD_KIND_PROC)
                    {
                        candidate_types[valid_count] = sym->value.type_info;
                        candidate_symbols[valid_count] = sym;
                        valid_count++;
                    }
                    else
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "candidate '%s' in overload bundle is not a procedure", id_node->text);
                        sem_error_list_add(&ctx->errors, NULL, id_node, buf);
                    }
                }
                if (valid_count > 0)
                {
                    TypeDescriptor const * bundle_type = get_or_create_overload_bundle_type(
                        ctx->type_registry, candidate_types, candidate_symbols, valid_count
                    );
                    value_node->resolved_type = (TypeDescriptor *)bundle_type;
                }
                free(candidate_types);
                free(candidate_symbols);
            }
        }
        else if (is_type_node(value_node) || value_node->type == AST_NODE_IDENTIFIER)
        {
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, value_node);
            if (td != NULL)
            {
                // Type alias: Handle :: int, Handle :: MyType
                value_node->resolved_type = (TypeDescriptor *)td;
                TypedValue tv = create_typed_value(NULL, td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (sym)
                    sym->kind = SYMBOL_TYPE;
                break;
            }
            sem_evaluate_expr(ctx, value_node);
        }
        else
        {
            sem_evaluate_expr(ctx, value_node);
        }

        TypeDescriptor const * val_type = value_node->resolved_type;
        TypedValue tv = create_typed_value(NULL, val_type, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);

        // Error: main proc must not have a return type; use os.exit() to set exit codes
        if (value_node->type == AST_NODE_PROCEDURE_DEFINITION && strcmp(name_node->text, "main") == 0)
        {
            if (val_type != NULL && val_type->kind == TD_KIND_PROC
                && val_type->proc_metadata.return_count > 0
                && val_type->proc_metadata.return_type != NULL
                && val_type->proc_metadata.return_type != type_descriptor_get_void_type(ctx->type_registry))
            {
                sem_error_list_add(&ctx->errors, NULL, name_node,
                                   "main procedure must not return a value; use os.exit() to set exit codes");
            }
        }
        break;
    }

    case AST_NODE_WHEN_STATEMENT:
    {
        // Same structure as IF_STATEMENT: children[0] = condition, children[1] = then-body,
        // subsequent children = else-when/else clauses
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
            {
                generator_push_scope(ctx->gen_ctx);
                sem_analyse_compound_statement(ctx, child, expected_return_type);
                generator_pop_scope(ctx->gen_ctx);
            }
            else
            {
                sem_evaluate_expr(ctx, child);
            }
        }
        break;
    }

    case AST_NODE_IF_STATEMENT:
    {
        // children[0] = condition, children[1] = then-body, children[2] = else-body (optional)
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
            {
                generator_push_scope(ctx->gen_ctx);
                sem_analyse_compound_statement(ctx, child, expected_return_type);
                generator_pop_scope(ctx->gen_ctx);
            }
            else if (child->type == AST_NODE_IF_STATEMENT)
            {
                sem_pass2_node(ctx, child, expected_return_type);
            }
            else if (child->type == AST_NODE_EXPRESSION_STATEMENT
                     || child->type == AST_NODE_ASSIGN_STATEMENT
                     || child->type == AST_NODE_VARIABLE_DECL
                     || child->type == AST_NODE_CONSTANT_DECL
                     || child->type == AST_NODE_RETURN_STATEMENT
                     || child->type == AST_NODE_BREAK_STATEMENT
                     || child->type == AST_NODE_CONTINUE_STATEMENT
                     || child->type == AST_NODE_DEFER_STATEMENT
                     || child->type == AST_NODE_FALLTHROUGH_STATEMENT
                     || child->type == AST_NODE_FOR_STATEMENT
                     || child->type == AST_NODE_SWITCH_STATEMENT
                     || child->type == AST_NODE_WHEN_STATEMENT)
            {
                // `if cond do stmt` form — analyse the statement directly
                generator_push_scope(ctx->gen_ctx);
                sem_pass2_node(ctx, child, expected_return_type);
                generator_pop_scope(ctx->gen_ctx);
            }
            else
            {
                sem_evaluate_expr(ctx, child);
            }
        }
        break;
    }

    case AST_NODE_FOR_STATEMENT:
    {
        odin_grammar_node_t * body = node_find_child(node, AST_NODE_COMPOUND_STATEMENT);

        // Detect for-range: first child is a raw Identifier
        bool is_for_range = false;
        if (node->list.count >= 2 && node->list.children[0] != NULL
            && node->list.children[0]->type == AST_NODE_IDENTIFIER)
        {
            for (size_t i = 1; i < node->list.count; i++)
            {
                odin_grammar_node_t * child = node->list.children[i];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_COMPOUND_STATEMENT)
                    break;
                if (child->type == AST_NODE_IDENTIFIER)
                    continue;
                sem_evaluate_expr(ctx, child);
                if (child->resolved_type && child->resolved_type->kind == TD_KIND_RANGE)
                {
                    is_for_range = true;
                }
                break;
            }
        }

        // For non-range for loops, evaluate condition expressions before pushing scope
        if (!is_for_range && node->list.count >= 1 && node->list.children[0] != NULL
            && node->list.children[0]->type != AST_NODE_COMPOUND_STATEMENT)
        {
            sem_evaluate_expr(ctx, node->list.children[0]);
        }

        generator_push_scope(ctx->gen_ctx);

        if (is_for_range)
        {
            TypeDescriptor const * i64_type = type_descriptor_get_int64_type(ctx->type_registry);
            for (size_t i = 0; i < node->list.count; i++)
            {
                odin_grammar_node_t * child = node->list.children[i];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_COMPOUND_STATEMENT)
                    break;
                if (child->type == AST_NODE_IDENTIFIER)
                {
                    TypedValue tv = create_typed_value(NULL, i64_type, true);
                    generator_add_symbol(ctx->gen_ctx, child->text, tv);
                }
            }
        }

        if (body)
        {
            sem_analyse_compound_statement(ctx, body, expected_return_type);
        }

        generator_pop_scope(ctx->gen_ctx);
        break;
    }

    case AST_NODE_SWITCH_STATEMENT:
    {
        // Detect #partial directive among switch children
        bool is_partial = false;
        bool has_default = false;
        odin_grammar_node_t * switch_expr_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_DIRECTIVE || child->type == AST_NODE_DIRECTIVE_WITH_ARGS)
            {
                if (child->text != NULL && strstr(child->text, "#partial") != NULL)
                    is_partial = true;
            }
            else if (child->type == AST_NODE_SWITCH_DEFAULT)
            {
                has_default = true;
            }
            else if (child->type != AST_NODE_SWITCH_CASE && child->type != AST_NODE_COMPOUND_STATEMENT)
            {
                // First non-directive, non-case, non-default, non-compound child is the switch expression
                if (switch_expr_node == NULL)
                    switch_expr_node = child;
            }
        }

        // Evaluate switch expression to determine its type
        TypeDescriptor const * switch_type = NULL;
        if (switch_expr_node != NULL)
        {
            sem_evaluate_expr(ctx, switch_expr_node);
            switch_type = switch_expr_node->resolved_type;
        }

        // Collect case values (enumerator values covered by the switch)
        // Only relevant if the switch type is an enum
        long long covered_values[64];
        int covered_count = 0;
        bool can_check_exhaustiveness = (switch_type != NULL && switch_type->kind == TD_KIND_ENUM
                                         && !has_default && !is_partial
                                         && switch_type->as.enum_type.enumerator_count > 0
                                         && switch_type->as.enum_type.enumerator_values != NULL);

        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_SWITCH_CASE)
            {
                generator_push_scope(ctx->gen_ctx);
                // Children: case value expression(s), then body statement(s)
                for (size_t j = 0; j < child->list.count; j++)
                {
                    odin_grammar_node_t * case_child = child->list.children[j];
                    if (case_child == NULL)
                        continue;
                    if (case_child->type == AST_NODE_COMPOUND_STATEMENT || case_child->type == AST_NODE_RETURN_STATEMENT
                        || case_child->type == AST_NODE_BREAK_STATEMENT
                        || case_child->type == AST_NODE_CONTINUE_STATEMENT
                        || case_child->type == AST_NODE_FALLTHROUGH_STATEMENT
                        || case_child->type == AST_NODE_EXPRESSION_STATEMENT
                        || case_child->type == AST_NODE_ASSIGN_STATEMENT || case_child->type == AST_NODE_VARIABLE_DECL
                        || case_child->type == AST_NODE_IF_STATEMENT || case_child->type == AST_NODE_FOR_STATEMENT
                        || case_child->type == AST_NODE_SWITCH_STATEMENT
                        || case_child->type == AST_NODE_DEFER_STATEMENT)
                    {
                        sem_pass2_node(ctx, case_child, expected_return_type);
                    }
                    else
                    {
                        sem_evaluate_expr(ctx, case_child);

                        // If we're tracking exhaustiveness, record the case value
                        if (can_check_exhaustiveness && covered_count < 64)
                        {
                            symbol_t * case_sym = NULL;
                            odin_grammar_node_t * ident = case_child;
                            // Unwrap expression wrappers to find identifier
                            while (ident != NULL
                                   && ident->type != AST_NODE_IDENTIFIER
                                   && ident->list.count > 0)
                            {
                                ident = ident->list.children[0];
                            }
                            // Try to get the constant int value of the case expression
                            if (ident != NULL && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                            {
                                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
                                if (sym && sym->has_const_int_val)
                                    covered_values[covered_count++] = sym->const_int_val;
                            }
                            else if (case_child->type == AST_NODE_INTEGER_VALUE)
                            {
                                // Direct integer case value
                                if (case_child->text != NULL)
                                    covered_values[covered_count++] = strtoll(case_child->text, NULL, 10);
                            }
                        }
                    }
                }
                generator_pop_scope(ctx->gen_ctx);
            }
            else if (child->type == AST_NODE_SWITCH_DEFAULT)
            {
                generator_push_scope(ctx->gen_ctx);
                for (size_t j = 0; j < child->list.count; j++)
                {
                    odin_grammar_node_t * def_child = child->list.children[j];
                    if (def_child == NULL)
                        continue;
                    sem_pass2_node(ctx, def_child, expected_return_type);
                }
                generator_pop_scope(ctx->gen_ctx);
            }
            else if (child->type == AST_NODE_COMPOUND_STATEMENT)
            {
                sem_analyse_compound_statement(ctx, child, expected_return_type);
            }
            else if (child->type != AST_NODE_DIRECTIVE && child->type != AST_NODE_DIRECTIVE_WITH_ARGS)
            {
                sem_evaluate_expr(ctx, child);
            }
        }

        // Exhaustiveness check: verify all enum values are covered
        if (can_check_exhaustiveness)
        {
            int num_enumerators = switch_type->as.enum_type.enumerator_count;
            char const ** enum_names = switch_type->as.enum_type.enumerator_names;
            long long * enum_values = switch_type->as.enum_type.enumerator_values;

            // Check each enumerator
            for (int ei = 0; ei < num_enumerators; ei++)
            {
                bool found = false;
                for (int ci = 0; ci < covered_count; ci++)
                {
                    if (covered_values[ci] == enum_values[ei])
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "switch is not exhaustive: missing case for enum value '%s'",
                             enum_names[ei] ? enum_names[ei] : "<unknown>");
                    sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
                }
            }
        }
        break;
    }

    case AST_NODE_BREAK_STATEMENT:
    case AST_NODE_CONTINUE_STATEMENT:
        break;

    case AST_NODE_DEFER_STATEMENT:
        if (node->list.count > 0)
        {
            sem_pass2_node(ctx, node->list.children[0], expected_return_type);
        }
        break;

    case AST_NODE_DIRECTIVE_WITH_ARGS:
    case AST_NODE_DIRECTIVE:
        break;

    case AST_NODE_WHERE_CLAUSE:
        break;

    case AST_NODE_FOREIGN_BLOCK:
    {
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_CONSTANT_DECL || child->type == AST_NODE_VARIABLE_DECL)
                sem_pass2_node(ctx, child, NULL);
        }
        break;
    }

    case AST_NODE_FOREIGN_IMPORT:
        break;

    case AST_NODE_USING_DECL:
    {
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_CONSTANT_DECL || child->type == AST_NODE_VARIABLE_DECL)
                sem_pass2_node(ctx, child, NULL);
        }
        break;
    }

    default:
        break;
    }
}

static void
sem_pass2_analyse_bodies_ast(SemContext * ctx, odin_grammar_node_t * program)
{
    if (program == NULL)
        return;

    for (size_t i = 0; i < program->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = program->list.children[i];
        if (ext_decl == NULL || ext_decl->type != AST_NODE_EXTERNAL_DECLARATIONS)
            continue;

        for (size_t j = 0; j < ext_decl->list.count; j++)
        {
            odin_grammar_node_t * top_decl = ext_decl->list.children[j];
            if (top_decl == NULL)
                continue;

            if (top_decl->type == AST_NODE_CONSTANT_DECL || top_decl->type == AST_NODE_VARIABLE_DECL)
            {
                sem_pass2_node(ctx, top_decl, NULL);
            }
            else if (top_decl->type == AST_NODE_FOREIGN_BLOCK)
            {
                for (size_t k = 0; k < top_decl->list.count; k++)
                {
                    odin_grammar_node_t * fb_child = top_decl->list.children[k];
                    if (fb_child == NULL)
                        continue;
                    if (fb_child->type == AST_NODE_CONSTANT_DECL || fb_child->type == AST_NODE_VARIABLE_DECL)
                        sem_pass2_node(ctx, fb_child, NULL);
                }
            }
            else if (top_decl->type == AST_NODE_USING_DECL)
            {
                for (size_t k = 0; k < top_decl->list.count; k++)
                {
                    odin_grammar_node_t * inner = top_decl->list.children[k];
                    if (inner == NULL)
                        continue;
                    if (inner->type == AST_NODE_CONSTANT_DECL || inner->type == AST_NODE_VARIABLE_DECL)
                        sem_pass2_node(ctx, inner, NULL);
                }
            }
            else if (top_decl->type == AST_NODE_WHEN_DECL)
            {
                size_t k = 0;
                bool matched = false;
                while (k < top_decl->list.count)
                {
                    odin_grammar_node_t * wc = top_decl->list.children[k];
                    if (wc == NULL)
                    {
                        k++;
                        continue;
                    }
                    if (wc->type == AST_NODE_WHEN_BODY)
                    {
                        if (!matched)
                        {
                            for (size_t m = 0; m < wc->list.count; m++)
                            {
                                odin_grammar_node_t * inner = wc->list.children[m];
                                if (inner == NULL)
                                    continue;
                                if (inner->type == AST_NODE_CONSTANT_DECL || inner->type == AST_NODE_VARIABLE_DECL)
                                    sem_pass2_node(ctx, inner, NULL);
                            }
                        }
                        break;
                    }
                    int cond = sem_evaluate_constant_bool(ctx, wc);
                    k++;
                    if (cond == 1 && !matched)
                    {
                        matched = true;
                        if (k < top_decl->list.count)
                        {
                            odin_grammar_node_t * body = top_decl->list.children[k];
                            if (body && body->type == AST_NODE_WHEN_BODY)
                            {
                                for (size_t m = 0; m < body->list.count; m++)
                                {
                                    odin_grammar_node_t * inner = body->list.children[m];
                                    if (inner == NULL)
                                        continue;
                                    if (inner->type == AST_NODE_CONSTANT_DECL || inner->type == AST_NODE_VARIABLE_DECL)
                                        sem_pass2_node(ctx, inner, NULL);
                                }
                            }
                        }
                    }
                    k++;
                }
            }
        }
    }
}

// --- Main entry point ---

bool
sem_analyse(SemContext * ctx)
{
    sem_pass1_register_top_level(ctx);
    if (sem_error_list_has_errors(&ctx->errors))
        return false;

    sem_pass2_analyse_bodies_ast(ctx, ctx->ast);
    if (sem_error_list_has_errors(&ctx->errors))
        return false;

    return true;
}

#include <stdio.h>
