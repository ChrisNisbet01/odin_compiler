#include "semantic_analyser.h"

#include "ast_utils.h"
#include "odin_grammar_ast.h"
#include "package_resolver.h"
#include "polymorphism.h"
#include "scope.h"
#include "sem_check.h"
#include "sem_context.h"
#include "sem_type_resolver.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for remaining static functions
static void sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type);
static void sem_pass1_register_top_level_ex(SemContext * ctx, odin_grammar_node_t * program_ast);
static void sem_pass2_analyse_bodies_ast(SemContext * ctx, odin_grammar_node_t * program);
static void sem_analyse_attributes(odin_grammar_node_t * decl_node);

// Detect whether a constant-decl value expression is an intrinsic reference
// (`intrinsics.type_is_float`, possibly wrapped in expression chain nodes).
// Returns the intrinsic proc symbol (from the auto-imported base:intrinsics
// package scope) or NULL if the value is not an intrinsic reference.
static symbol_t *
sem_find_intrinsic_from_value(odin_grammar_node_t * value_node)
{
    if (value_node == NULL)
        return NULL;

    // Unwrap single-child expression wrappers (ExpressionOrStructLit/Expression)
    while (value_node->list.count == 1 && value_node->list.children[0] != NULL)
        value_node = value_node->list.children[0];

    if (value_node->type != AST_NODE_POSTFIX_EXPRESSION)
        return NULL;

    for (size_t i = 0; i < value_node->list.count; i++)
    {
        odin_grammar_node_t * child = value_node->list.children[i];
        if (child != NULL && child->type == AST_NODE_POSTFIX_OPS && child->list.count > 0)
        {
            for (size_t j = child->list.count; j > 0; j--)
            {
                odin_grammar_node_t * op = child->list.children[j - 1];
                if (op == NULL)
                    continue;
                if (op->type != AST_NODE_POSTFIX_MEMBER)
                    break; // the trailing op is a call/etc — not a bare intrinsic ref
                if (op->resolved_symbol != NULL && poly_is_known_intrinsic_name(op->resolved_symbol->name))
                    return op->resolved_symbol;
                return NULL;
            }
        }
    }
    return NULL;
}

// --- Compile-time constant integer evaluation ---
// Evaluates a constant expression to an integer at compile time.
// Returns the value and sets *ok = 1 on success, or sets *ok = 0 on failure.

static long long
sem_eval_const_identifier(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    char const * name = node->text;
    if (name == NULL)
    {
        *ok = 0;
        return 0;
    }

    // For poly_idents, look up in poly environment
    if (node->type == AST_NODE_POLY_IDENT && name[0] == '$')
    {
        name++; // Skip the $ prefix
    }
    else if (node->type == AST_NODE_IDENTIFIER)
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name);
        if (sym != NULL && sym->has_const_int_val)
        {
            *ok = 1;
            return sym->const_int_val;
        }
        *ok = 0;
        return 0;
    }

    long long val = 0;
    if (poly_env_lookup_int(ctx, name, &val))
    {
        *ok = 1;
        return val;
    }
    *ok = 0;
    return 0;
}

static long long
sem_eval_const_postfix(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    // Package-qualified constant: e.g. os.O_WRONLY
    if (node->list.count < 2 || node->list.children[0] == NULL || node->list.children[1] == NULL)
    {
        *ok = 0;
        return 0;
    }

    odin_grammar_node_t * inner = node->list.children[0];
    // Unwrap to identifier
    while (inner != NULL && inner->type != AST_NODE_IDENTIFIER && inner->list.count >= 1)
        inner = inner->list.children[0];
    if (inner == NULL || inner->type != AST_NODE_IDENTIFIER)
    {
        *ok = 0;
        return 0;
    }

    ImportedPackage * pkg = find_imported_package_by_name(ctx, inner->text);
    if (pkg == NULL || pkg->package_scope == NULL)
    {
        *ok = 0;
        return 0;
    }

    odin_grammar_node_t * postfix_ops = node->list.children[1];
    if (postfix_ops == NULL || postfix_ops->list.count == 0)
    {
        *ok = 0;
        return 0;
    }

    odin_grammar_node_t * member_op = postfix_ops->list.children[0];
    if (member_op == NULL || member_op->type != AST_NODE_POSTFIX_MEMBER)
    {
        *ok = 0;
        return 0;
    }

    if (member_op->list.count < 1 || member_op->list.children[0] == NULL)
    {
        *ok = 0;
        return 0;
    }

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

static long long
sem_eval_const_integer(odin_grammar_node_t * node, int * ok)
{
    if (node->text == NULL)
    {
        *ok = 0;
        return 0;
    }
    char * end = NULL;
    long long val = parse_odin_signed(node->text, &end, 0);
    if (end == node->text)
    {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return val;
}

static long long
sem_eval_const_unary(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node == NULL)
    {
        *ok = 0;
        return 0;
    }
    AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
    if (md == NULL)
    {
        *ok = 0;
        return 0;
    }

    odin_grammar_node_t * operand = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && child != op_node)
        {
            operand = child;
            break;
        }
    }
    if (operand == NULL)
    {
        *ok = 0;
        return 0;
    }

    int inner_ok = 0;
    long long inner_val = sem_evaluate_constant_int(ctx, operand, &inner_ok);
    if (!inner_ok)
    {
        *ok = 0;
        return 0;
    }

    switch (md->kind)
    {
    case OP_UNARY_NEG:
        *ok = 1;
        return -inner_val;
    case OP_UNARY_POS:
        *ok = 1;
        return inner_val;
    case OP_UNARY_XOR:
        *ok = 1;
        return ~inner_val;
    case OP_UNARY_NOT:
        *ok = 1;
        return inner_val ? 0 : 1;
    default:
        *ok = 0;
        return 0;
    }
}

static long long
sem_eval_const_binary(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node == NULL)
    {
        *ok = 0;
        return 0;
    }
    AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
    if (md == NULL)
    {
        *ok = 0;
        return 0;
    }
    if (node->list.count < 3)
    {
        *ok = 0;
        return 0;
    }

    int lhs_ok = 0, rhs_ok = 0;
    long long lhs_val = sem_evaluate_constant_int(ctx, node->list.children[0], &lhs_ok);
    long long rhs_val = sem_evaluate_constant_int(ctx, node->list.children[node->list.count - 1], &rhs_ok);
    if (!lhs_ok || !rhs_ok)
    {
        *ok = 0;
        return 0;
    }

    switch (md->kind)
    {
    case OP_ADD:
        *ok = 1;
        return lhs_val + rhs_val;
    case OP_SUB:
        *ok = 1;
        return lhs_val - rhs_val;
    case OP_MUL:
        *ok = 1;
        return lhs_val * rhs_val;
    case OP_DIV:
        if (rhs_val == 0)
        {
            *ok = 0;
            return 0;
        }
        *ok = 1;
        return lhs_val / rhs_val;
    case OP_MOD:
        if (rhs_val == 0)
        {
            *ok = 0;
            return 0;
        }
        *ok = 1;
        return lhs_val % rhs_val;
    case OP_SHL:
        *ok = 1;
        return lhs_val << rhs_val;
    case OP_SHR:
        *ok = 1;
        return lhs_val >> rhs_val;
    case OP_BIT_AND:
        *ok = 1;
        return lhs_val & rhs_val;
    case OP_BIT_OR:
        *ok = 1;
        return lhs_val | rhs_val;
    case OP_BIT_XOR:
        *ok = 1;
        return lhs_val ^ rhs_val;
    case OP_EQ:
        *ok = 1;
        return (lhs_val == rhs_val) ? 1 : 0;
    case OP_NE:
        *ok = 1;
        return (lhs_val != rhs_val) ? 1 : 0;
    case OP_LT:
        *ok = 1;
        return (lhs_val < rhs_val) ? 1 : 0;
    case OP_GT:
        *ok = 1;
        return (lhs_val > rhs_val) ? 1 : 0;
    case OP_LE:
        *ok = 1;
        return (lhs_val <= rhs_val) ? 1 : 0;
    case OP_GE:
        *ok = 1;
        return (lhs_val >= rhs_val) ? 1 : 0;
    default:
        *ok = 0;
        return 0;
    }
}

long long
sem_evaluate_constant_int(SemContext * ctx, odin_grammar_node_t * node, int * ok)
{
    if (node == NULL)
    {
        *ok = 0;
        return 0;
    }

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
        case AST_NODE_POLY_IDENT:
            can_eval = 1;
            break;
        case AST_NODE_POSTFIX_EXPRESSION:
            // Don't unwrap if it has postfix member (e.g. os.O_WRONLY) — evaluate it
            if (node->list.count >= 2 && node->list.children[1] != NULL)
            {
                odin_grammar_node_t * postfix_ops = node->list.children[1];
                if (postfix_ops->list.count > 0 && postfix_ops->list.children[0] != NULL
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
        {
            *ok = 0;
            return 0;
        }
    }

    switch (node->type)
    {
    case AST_NODE_BOOL_TRUE:
        *ok = 1;
        return 1;
    case AST_NODE_BOOL_FALSE:
        *ok = 1;
        return 0;

    case AST_NODE_IDENTIFIER:
    case AST_NODE_POLY_IDENT:
        return sem_eval_const_identifier(ctx, node, ok);
    case AST_NODE_POSTFIX_EXPRESSION:
        return sem_eval_const_postfix(ctx, node, ok);
    case AST_NODE_INTEGER_VALUE:
        return sem_eval_const_integer(node, ok);
    case AST_NODE_UNARY_EXPRESSION:
        return sem_eval_const_unary(ctx, node, ok);
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
        return sem_eval_const_binary(ctx, node, ok);
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
            // For named multi-return with bare return, allow it
            if (node->list.count == 0 && pm->named_return_names != NULL && pm->return_count > 0)
            {
                // Named multi-return - bare return is valid, all named vars will be used
                return;
            }
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
            // Check for named return before updating expected_return_type
            if (node->list.count == 0 && pm->named_return_names != NULL)
            {
                // Named single return - bare return is valid
                return;
            }
            expected_return_type = pm->returns[0];
        }
        else
        {
            // Multiple return types - if no expression, check for named returns
            if (node->list.count == 0 && pm->named_return_names != NULL && pm->return_count > 0)
            {
                // Named multi-return - bare return is valid
                return;
            }
            if (node->list.count > 0)
                sem_error_list_add(&ctx->errors, NULL, node, "unexpected return value in void procedure");
            return;
        }
    }

    if (node->list.count == 0)
    {
        // Check for single or multi named return - this is valid
        if (expected_return_type != NULL && expected_return_type->kind == TD_KIND_PROC)
        {
            ProcMetadata const * pm = &expected_return_type->proc_metadata;
            if (pm->named_return_names != NULL && pm->return_count > 0)
            {
                // Named return with implicit return - valid, IR gen will handle it
                return;
            }
        }
        if (expected_return_type != NULL && expected_return_type != type_descriptor_get_void_type(ctx->type_registry))
        {
            sem_error_list_add(&ctx->errors, ctx->source_file_path, node, "expected return value");
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


static void
sem_resolve_proc_returns_clause(SemContext * ctx,
    odin_grammar_node_t * ret_child,
    TypeDescriptor const ** out_return_type,
    TypeDescriptor const *** out_return_types,
    int * out_return_count,
    char const *** out_named_return_names,
    bool * out_have_named_return_names)
{
    if (ret_child->type == AST_NODE_RETURN_TYPE_LIST)
    {
        (*out_return_type) = NULL;
        (*out_return_count) = 0;
        (*out_return_types) = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
        for (size_t ri = 0; ri < ret_child->list.count; ri++)
        {
            odin_grammar_node_t * tn = ret_child->list.children[ri];
            if (tn == NULL)
                continue;
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, tn);
            tn->resolved_type = td;
            if (td)
                (*out_return_types)[(*out_return_count)++] = td;
        }
    }
    else if (ret_child->type == AST_NODE_RETURN_LIST)
    {
        (*out_return_type) = NULL;
        (*out_return_count) = 0;
        (*out_return_types) = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
        (*out_named_return_names) = calloc(ret_child->list.count, sizeof(char const *));
        (*out_have_named_return_names) = true;
        for (size_t ri = 0; ri < ret_child->list.count; ri++)
        {
            odin_grammar_node_t * named = ret_child->list.children[ri];
            if (named == NULL || named->type != AST_NODE_NAMED_RETURN)
                continue;
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * type_node = NULL;
            for (size_t ci = 0; ci < named->list.count; ci++)
            {
                odin_grammar_node_t * ch = named->list.children[ci];
                if (ch == NULL)
                    continue;
                if (ch->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = ch;
                else if (ch->type != AST_NODE_DIRECTIVE && ch->type != AST_NODE_DIRECTIVE_WITH_ARGS)
                    type_node = ch;
            }
            if (type_node == NULL)
                continue;
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
            type_node->resolved_type = td;
            if (td)
            {
                (*out_return_types)[(*out_return_count)] = td;
                (*out_named_return_names)[(*out_return_count)] = name_node ? name_node->text : NULL;
                (*out_return_count)++;
            }
        }
    }
    else
    {
        (*out_return_type) = sem_resolve_type_expr(ctx, ret_child);
        if ((*out_return_type))
        {
            (*out_return_types) = malloc(sizeof(TypeDescriptor const *));
            (*out_return_types)[0] = (*out_return_type);
            (*out_return_count) = 1;
        }
    }
}

static bool
sem_resolve_proc_params_from_list(SemContext * ctx,
    odin_grammar_node_t * param_list_node,
    TypeDescriptor const *** out_param_types,
    int * out_param_count,
    size_t * inout_param_cap,
    odin_grammar_node_t *** inout_default_nodes,
    size_t * inout_default_cap,
    bool * inout_is_variadic)
{
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

                // Handle multi-name params: "a, b: T" -> names {a, b}, one type.
                odin_grammar_node_t * param_names[32];
                odin_grammar_node_t * param_type_node = NULL;
                bool is_poly_decl_param = false;
                int name_count = sem_extract_param_names(param, param_names, 32, &param_type_node, &is_poly_decl_param);

                // Detect default value: last child that is not a name,
                // not the type node, and not an ellipsis
                odin_grammar_node_t * default_value_node = NULL;
                for (size_t ci = param->list.count; ci > 0; ci--)
                {
                    odin_grammar_node_t * child = param->list.children[ci - 1];
                    if (child == NULL)
                        continue;
                    bool is_name = false;
                    for (int ni = 0; ni < name_count; ni++)
                    {
                        if (child == param_names[ni])
                        {
                            is_name = true;
                            break;
                        }
                    }
                    if (is_name || child == param_type_node)
                        continue;
                    if (child->type == AST_NODE_ELLIPSIS)
                        continue;
                    if (!is_type_node(child))
                    {
                        default_value_node = child;
                        break;
                    }
                }
                if (name_count == 0 || param_type_node == NULL)
                    continue;

                TypeDescriptor const * pt = sem_resolve_type_expr(ctx, param_type_node);
                if (pt == NULL)
                    continue;
                param_type_node->resolved_type = pt;

                // If this is a variadic .. parameter, wrap the type in a slice
                bool is_variadic_param = false;
                for (size_t ci = 0; ci < param->list.count; ci++)
                {
                    if (param->list.children[ci] != NULL && param->list.children[ci]->type == AST_NODE_ELLIPSIS)
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
                    param_type_node->resolved_type = pt;
                    (*inout_is_variadic) = true;
                }

                // Skip poly type parameters ($T: typeid) — they don't consume
                // runtime parameter slots in the LLVM function type.
                if (is_poly_decl_param)
                {
                    continue;
                }

                // Register each name as a separate runtime param slot.
                // The default value applies only to the last name in the group
                // (a, b: int = 10 -> b defaults to 10, a does not).
                for (int ni = 0; ni < name_count; ni++)
                {
                    if ((*out_param_count) >= (int)(*inout_param_cap))
                    {
                        size_t new_cap = (*inout_param_cap) == 0 ? 4 : (*inout_param_cap) * 2;
                        TypeDescriptor const ** tmp = realloc((*out_param_types), new_cap * sizeof(TypeDescriptor const *));
                        if (tmp == NULL)
                        {
                            free((*out_param_types));
                            free((*inout_default_nodes));
                            return false;
                        }
                        (*out_param_types) = tmp;
                        (*inout_param_cap) = new_cap;
                    }
                    // Store default value node (NULL if no default)
                    if ((*out_param_count) >= (int)(*inout_default_cap))
                    {
                        size_t new_cap = (*inout_default_cap) == 0 ? 4 : (*inout_default_cap) * 2;
                        odin_grammar_node_t ** tmp2
                            = realloc((*inout_default_nodes), new_cap * sizeof(odin_grammar_node_t *));
                        if (tmp2 == NULL)
                        {
                            free((*out_param_types));
                            free((*inout_default_nodes));
                            return false;
                        }
                        // Zero-initialize newly allocated slots
                        for (size_t zi = (*inout_default_cap); zi < new_cap; zi++)
                            tmp2[zi] = NULL;
                        (*inout_default_nodes) = tmp2;
                        (*inout_default_cap) = new_cap;
                    }
                    (*inout_default_nodes)[(*out_param_count)] = (ni == name_count - 1) ? default_value_node : NULL;
                    (*out_param_types)[(*out_param_count)++] = pt;
                }
            }
        }
    }

    // Also detect bare ... variadic
    if (!(*inout_is_variadic) && param_list_node != NULL && param_list_node->list.count > 0)
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
                    (*inout_is_variadic) = true;
                    break;
                }
            }
        }
    }

    return true;
}

static TypeDescriptor const *
sem_resolve_procedure_signature(
    SemContext * ctx,
    odin_grammar_node_t * node,
    TypeDescriptor const *** out_param_types,
    int * out_param_count,
    TypeDescriptor const ** out_return_type,
    int * out_return_count,
    calling_convention_t * out_cc,
    bool * out_is_variadic
)
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

    // Variables for named return handling
    odin_grammar_node_t ** default_value_nodes = NULL;
    size_t default_val_cap = 0;
    char const ** named_return_names_array = NULL;
    bool have_named_return_names = false;

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
                    sem_resolve_proc_returns_clause(ctx, sig_child->list.children[0], &return_type,
                        &return_types, &return_count, &named_return_names_array, &have_named_return_names);
                }
                else if (sig_child->type == AST_NODE_PARAMETER_LIST)
                {
                    param_list_node = sig_child;
                }
            }
        }
    }

    // Extract param type descriptors and default values from the parameter list
    // (also detects bare ... variadic)
    if (!sem_resolve_proc_params_from_list(ctx, param_list_node, &param_types, &param_count,
        &param_cap, &default_value_nodes, &default_val_cap, &is_variadic))
    {
        return NULL;
    }
    // Create the procedure type descriptor
    if (return_count == 0)
        return_types = NULL;

    // Check if any parameter has a default value — force unique type to
    // avoid sharing default_values across procs with same signature
    bool has_any_defaults = false;
    if (default_value_nodes != NULL)
    {
        for (int di = 0; di < param_count; di++)
        {
            if (default_value_nodes[di] != NULL)
            {
                has_any_defaults = true;
                break;
            }
        }
    }

    TypeDescriptor const * proc_type = get_or_create_proc_type(
        ctx->type_registry,
        return_type,
        param_types,
        param_count,
        return_types,
        return_count,
        is_variadic,
        cc,
        named_return_names_array,
        has_any_defaults || have_named_return_names // force_unique
    );

    // Free the named_return_names array if we allocated it
    if (have_named_return_names && named_return_names_array != NULL)
    {
        for (int i = 0; i < return_count; i++)
        {
            // Names are string literals, no need to free individual entries
        }
        free((void *)named_return_names_array);
    }

    // Populate default parameter values in the proc type metadata
    if (proc_type != NULL && default_value_nodes != NULL && param_count > 0)
    {
        // Cast is intentional: stores per-signature default-value AST nodes in the
        // shared descriptor after its const registry handle has been handed out.
        TypeDescriptor * mutable_proc = (TypeDescriptor *)proc_type;
        for (int di = 0; di < param_count; di++)
        {
            if (default_value_nodes[di] != NULL)
            {
                mutable_proc->proc_metadata.default_values[di] = default_value_nodes[di];
            }
        }
    }
    free(default_value_nodes);

    if (out_param_types)
        *out_param_types = param_types;
    else
        free(param_types);
    if (out_param_count)
        *out_param_count = param_count;
    if (out_return_type)
        *out_return_type = return_type;
    if (out_return_count)
        *out_return_count = return_count;
    if (out_cc)
        *out_cc = cc;
    if (out_is_variadic)
        *out_is_variadic = is_variadic;

    if (return_types && out_return_count == NULL)
        free((void *)return_types);

    node->resolved_type = proc_type;
    return proc_type;
}

void
sem_analyse_procedure_literal(SemContext * ctx, odin_grammar_node_t * node, char const * proc_name)
{
    TypeDescriptor const * return_type = NULL;
    TypeDescriptor const ** return_types = NULL;
    int return_count = 0;
    int param_count = 0;
    TypeDescriptor const ** param_types = NULL;
    calling_convention_t cc = CALLING_CONV_ODIN;
    bool is_variadic = false;

    // Polymorphic procedures are not analyzed standalone. Their bodies are
    // instantiated as specializations at each call site (Stage 3+ of the
    // polymorphism work). Without this early-return, references to $T / $N
    // in the body would fail to resolve and produce spurious semantic errors.
    // The `ctx->currently_instantiating` guard allows Stage-3 instantiation
    // to run normal body analysis on a cloned specialization whose
    // polymorphism has already been substituted away.
    if (!ctx->currently_instantiating && poly_signature_is_polymorphic(node))
    {
        return;
    }

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
    node->resolved_type = proc_type;

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

                    // Handle multi-name params: "a, b: T" -> register each name.
                    odin_grammar_node_t * param_names[32];
                    odin_grammar_node_t * param_type_node = NULL;
                    bool is_poly_decl_param = false;
                    int name_count
                        = sem_extract_param_names(param, param_names, 32, &param_type_node, &is_poly_decl_param);
                    if (name_count == 0 || param_type_node == NULL)
                        continue;

                    TypeDescriptor const * param_type = param_type_node->resolved_type;
                    if (param_type == NULL)
                        continue;

                    for (int ni = 0; ni < name_count; ni++)
                    {
                        char const * param_name = param_names[ni]->text;
                        // For poly params ($T), register with the base name (strip $)
                        if (param_names[ni]->type == AST_NODE_POLY_IDENT && param_name != NULL && param_name[0] == '$')
                            param_name = param_name + 1;

                        TypedValue tv = create_typed_value(NULL, param_type, true);
                        generator_add_symbol(ctx->gen_ctx, param_name, tv);
                    }
                }
            }
        }
    }

    // Register named return variables in the body scope
    // (they need to be declared for the semantic analyzer to resolve them)
    if (node->resolved_type != NULL && node->resolved_type->kind == TD_KIND_PROC)
    {
        TypeDescriptor const * proc_type = node->resolved_type;
        if (proc_type->proc_metadata.named_return_names != NULL && proc_type->proc_metadata.return_count > 0)
        {
            for (int ri = 0; ri < proc_type->proc_metadata.return_count; ri++)
            {
                char const * name = proc_type->proc_metadata.named_return_names[ri];
                if (name == NULL)
                    continue;
                TypeDescriptor const * ret_type = proc_type->proc_metadata.returns[ri];
                if (ret_type == NULL)
                    continue;
                TypedValue tv = create_typed_value(NULL, ret_type, true);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name, tv);
            }
        }
    }

    // Register polymorphic integer slots ($N) as i64 constants in body scope

    // Register polymorphic integer slots ($N) as i64 constants in body scope
    if (ctx->poly_env_stack_depth > 0)
    {
        PolyEnv * env = &ctx->poly_env_stack[ctx->poly_env_stack_depth - 1];
        TypeDescriptor const * i64_type = type_descriptor_get_int64_type(ctx->type_registry);
        for (int ei = 0; ei < env->count; ei++)
        {
            if (env->entries[ei].kind == POLY_SLOT_INT && env->entries[ei].name != NULL)
            {
                TypedValue tv = create_typed_value(NULL, i64_type, true);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), env->entries[ei].name, tv);
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), env->entries[ei].name);
                if (sym)
                {
                    sym->has_const_int_val = true;
                    sym->const_int_val = env->entries[ei].bound_int_value;
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
        TypeDescriptor const * expected_ret
            = node->resolved_type ? node->resolved_type : type_descriptor_get_void_type(ctx->type_registry);
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

// Resolve the candidates of an overload bundle (PROC_OVERLOAD_BUNDLE) against
// the current scope. On success returns the bundle TypeDescriptor and stores it
// on value_node->resolved_type. When a candidate is not (yet) a registered
// procedure or polymorphic symbol, *all_ok is set to false and an error is only
// emitted when emit_errors is true (used for the deferred/resolved path).
static TypeDescriptor const *
sem_resolve_overload_bundle(SemContext * ctx, odin_grammar_node_t * value_node, bool emit_errors, bool * all_ok)
{
    *all_ok = true;
    int candidate_count = (int)value_node->list.count;
    if (candidate_count <= 0)
        return NULL;

    TypeDescriptor const ** candidate_types
        = (TypeDescriptor const **)malloc((size_t)candidate_count * sizeof(TypeDescriptor const *));
    symbol_t ** candidate_symbols = (symbol_t **)malloc((size_t)candidate_count * sizeof(symbol_t *));
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
        else if (sym && sym->is_polymorphic)
        {
            candidate_types[valid_count] = NULL;
            candidate_symbols[valid_count] = sym;
            valid_count++;
        }
        else
        {
            if (emit_errors)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "candidate '%s' in overload bundle is not a procedure", id_node->text);
                sem_error_list_add(&ctx->errors, NULL, id_node, buf);
            }
            *all_ok = false;
        }
    }

    TypeDescriptor const * resolved = NULL;
    if (valid_count > 0)
    {
        resolved
            = get_or_create_overload_bundle_type(ctx->type_registry, candidate_types, candidate_symbols, valid_count);
        value_node->resolved_type = resolved;
    }
    free(candidate_types);
    free(candidate_symbols);
    return resolved;
}

// Record an overload bundle whose candidates could not all be resolved when the
// bundle declaration was processed (forward reference). The bundle's ConstantDecl
// is kept until the end of pass 1 and then resolved by sem_resolve_pending_bundles.
static void
sem_add_pending_bundle(SemContext * ctx, odin_grammar_node_t * node)
{
    if (ctx->pending_bundle_count >= ctx->pending_bundle_capacity)
    {
        int new_cap = ctx->pending_bundle_capacity == 0 ? 8 : ctx->pending_bundle_capacity * 2;
        odin_grammar_node_t ** new_arr = realloc(ctx->pending_bundles, (size_t)new_cap * sizeof(odin_grammar_node_t *));
        if (new_arr == NULL)
        {
            perror("realloc");
            exit(1);
        }
        ctx->pending_bundles = new_arr;
        ctx->pending_bundle_capacity = new_cap;
    }
    ctx->pending_bundles[ctx->pending_bundle_count++] = node;
}

// Resolve overload bundles deferred during pass 1 (entries [start_index, count)).
// Runs after all top-level declarations of the current file have been registered,
// so forward references to candidate procs now resolve. Errors for genuinely
// missing candidates are reported here. Already-resolved entries are discarded.
static void
sem_resolve_pending_bundles(SemContext * ctx, int start_index)
{
    for (int i = start_index; i < ctx->pending_bundle_count; i++)
    {
        odin_grammar_node_t * bundle_decl = ctx->pending_bundles[i];
        if (bundle_decl == NULL)
            continue;
        odin_grammar_node_t * name_node = node_find_child(bundle_decl, AST_NODE_IDENTIFIER);
        if (name_node == NULL)
            continue;
        odin_grammar_node_t * value_node = NULL;
        for (size_t j = 0; j < bundle_decl->list.count; j++)
        {
            odin_grammar_node_t * child = bundle_decl->list.children[j];
            if (child != NULL && child != name_node && child->type != AST_NODE_ATTRIBUTE)
            {
                value_node = child;
                break;
            }
        }
        if (value_node == NULL || value_node->type != AST_NODE_PROC_OVERLOAD_BUNDLE)
            continue;

        bool all_ok = true;
        TypeDescriptor const * resolved = sem_resolve_overload_bundle(ctx, value_node, true, &all_ok);
        (void)all_ok;

        // Back-fill the bundle symbol's type (registered earlier with NULL).
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
        if (sym)
            sym->value.type_info = resolved;
    }
    // Drop the entries we just resolved; they will not be revisited.
    ctx->pending_bundle_count = start_index;
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
        bool all_ok = true;
        resolved_type = sem_resolve_overload_bundle(ctx, value_node, false, &all_ok);
        if (!all_ok)
        {
            // One or more candidates are not registered yet. Odin allows a
            // proc group to be declared before the procs it contains, so the
            // resolution is deferred until pass 1 completes (all top-level
            // declarations registered). The bundle symbol is still registered
            // below (with a NULL type for now) and its type is filled in by
            // sem_resolve_pending_bundles.
            sem_add_pending_bundle(ctx, node);
            resolved_type = NULL;
        }
    }
    else if (value_node != NULL && is_type_node(value_node))
    {
        resolved_type = sem_resolve_type_expr(ctx, value_node);
        value_node->resolved_type = resolved_type;
    }
    else if (value_node != NULL && value_node->type == AST_NODE_IDENTIFIER)
    {
        resolved_type = sem_resolve_type_expr(ctx, value_node);
        value_node->resolved_type = resolved_type;
    }

    // Store resolved type on value_node for procedure definitions
    if (value_node != NULL && value_node->type == AST_NODE_PROCEDURE_DEFINITION)
    {
        value_node->resolved_type = resolved_type;
    }

    if (name_node->type == AST_NODE_IDENTIFIER)
    {
        TypedValue tv = create_typed_value(NULL, resolved_type, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
        sem_set_symbol_private(generator_current_scope(ctx->gen_ctx), name_node->text, is_private);

        // Mark type aliases (struct, union, enum, etc.) as SYMBOL_TYPE
        // Do this BEFORE any other code might look up this symbol
        if (resolved_type != NULL && value_node != NULL && value_node->type != AST_NODE_PROCEDURE_DEFINITION
            && value_node->type != AST_NODE_PROC_OVERLOAD_BUNDLE)
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
                sym->kind = SYMBOL_TYPE;
        }

        // Also mark procedure definitions as SYMBOL_PROCEDURE
        if (value_node != NULL && value_node->type == AST_NODE_PROCEDURE_DEFINITION)
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
                sym->kind = SYMBOL_PROCEDURE;
        }

        // Mark the symbol as polymorphic if its procedure signature uses any
        // $T / $N poly identifiers. Polymorphic procs are NOT analyzed or
        // codegen'd standalone; call sites (Stage 3+) instantiate specializations.
        if (value_node != NULL && value_node->type == AST_NODE_PROCEDURE_DEFINITION
            && poly_signature_is_polymorphic(value_node))
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
            {
                sym->is_polymorphic = true;
                // Store the origin ConstantDecl AST for later instantiation
                poly_register_origin(sym, node);
            }
        }

        // Mark the symbol as polymorphic if its struct type uses any
        // $T / $N poly identifiers in its parameter list. Polymorphic struct
        // types are NOT resolved standalone; usage sites instantiate
        // specializations via TypeApplication (e.g., Box(int)).
        if (value_node != NULL && value_node->type == AST_NODE_STRUCT_TYPE && poly_struct_has_type_params(value_node))
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
            {
                sym->is_polymorphic = true;
                sym->kind = SYMBOL_TYPE;
                // Store the origin ConstantDecl AST for later instantiation
                poly_register_origin(sym, node);
            }
        }

        // Mark the symbol as polymorphic if its enum type uses any
        // $T / $N poly identifiers in its parameter list.
        if (value_node != NULL && value_node->type == AST_NODE_ENUM_TYPE && poly_type_has_type_params(value_node))
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
            {
                sym->is_polymorphic = true;
                sym->kind = SYMBOL_TYPE;
                poly_register_origin(sym, node);
            }
        }

        // Mark the symbol as polymorphic if its union type uses any
        // $T / $N poly identifiers in its parameter list.
        if (value_node != NULL && value_node->type == AST_NODE_UNION_TYPE && poly_type_has_type_params(value_node))
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym)
            {
                sym->is_polymorphic = true;
                sym->kind = SYMBOL_TYPE;
                poly_register_origin(sym, node);
            }
        }

        // Try to evaluate as a compile-time integer constant
        if (value_node != NULL && value_node->type != AST_NODE_PROCEDURE_DEFINITION
            && value_node->type != AST_NODE_PROC_OVERLOAD_BUNDLE)
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
    symbol_t * copy = scope_find_symbol_entry(target_scope, sym->name);
    if (copy == NULL)
        return;
    if (sym->has_const_int_val)
    {
        copy->const_int_val = sym->const_int_val;
        copy->has_const_int_val = true;
    }
    // Propagate kind (SYMBOL_TYPE for type aliases)
    copy->kind = sym->kind;
    // Propagate polymorphism flag and register origin AST so poly calls
    // through the `using`-imported copy can resolve (Stage 11).
    if (sym->is_polymorphic)
    {
        copy->is_polymorphic = true;
        odin_grammar_node_t * origin = poly_get_origin(sym);
        if (origin != NULL)
            poly_register_origin(copy, origin);
    }
    // Propagate LLVM name so the copy is codegen'd with the mangled name
    if (sym->llvm_name)
        copy->llvm_name = strdup(sym->llvm_name);
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
sem_apply_attr_item(ProcDeclAttributes * attrs, odin_grammar_node_t * name_node, odin_grammar_node_t * value_node)
{
    if (attrs == NULL || name_node == NULL || name_node->text == NULL)
        return;

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
    else if (strcmp(name_node->text, "init") == 0)
    {
        attrs->is_init = true;
    }
    else if (strcmp(name_node->text, "fini") == 0)
    {
        attrs->is_fini = true;
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

    ProcDeclAttributes * attrs = calloc(1, sizeof(ProcDeclAttributes));

    // Bare attribute form: @private, @builtin, etc. — the child is an
    // Identifier (name) with no value.
    for (size_t i = 0; i < first->list.count; i++)
    {
        odin_grammar_node_t * child = first->list.children[i];
        if (child != NULL && child->type == AST_NODE_IDENTIFIER)
        {
            sem_apply_attr_item(attrs, child, NULL);
        }
    }

    // Parenthesized form: @(attr, ...) — the child is an ATTR_LIST.
    for (size_t i = 0; i < first->list.count; i++)
    {
        odin_grammar_node_t * attr_list = first->list.children[i];
        if (attr_list == NULL || attr_list->type != AST_NODE_ATTR_LIST)
            continue;
        for (size_t j = 0; j < attr_list->list.count; j++)
        {
            odin_grammar_node_t * item = attr_list->list.children[j];
            if (item == NULL || item->type != AST_NODE_ATTR_ITEM)
                continue;
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * value_node = NULL;
            for (size_t k = 0; k < item->list.count; k++)
            {
                odin_grammar_node_t * child = item->list.children[k];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER)
                    name_node = child;
                else
                    value_node = child;
            }
            sem_apply_attr_item(attrs, name_node, value_node);
        }
    }

    decl_node->metadata = attrs;
}

static bool
has_odin_extension(char const * path)
{
    if (path == NULL)
        return false;
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

// Auto-import base:runtime as an implicit using import (prelude)
static void
sem_pass1_auto_import_runtime(SemContext * ctx)
{
    // Auto-import base:runtime as an implicit using import (prelude)
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
        char * runtime_path = resolve_import_path("base:runtime", ctx->source_dir, ctx->odin_root);
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
                // core:runtime is an implicit prelude, not a user-declared import.
                // is_direct_import defaults to false (calloc), so leave it unset.
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
}
// Auto-import base:intrinsics as a named (non-using) import (prelude)
static void
sem_pass1_auto_import_intrinsics(SemContext * ctx)
{
    // Auto-import base:intrinsics as a named (non-using) import (prelude)
    // so `intrinsics.type_is_float` etc. resolve without an explicit import.
    bool intrinsics_already_imported = false;
    for (int ii = 0; ii < ctx->import_count; ii++)
    {
        if (ctx->imports[ii] && ctx->imports[ii]->is_intrinsics)
        {
            intrinsics_already_imported = true;
            break;
        }
    }
    if (!intrinsics_already_imported)
    {
        char * intrinsics_path = resolve_import_path("base:intrinsics", ctx->source_dir, ctx->odin_root);
        if (intrinsics_path)
        {
            ImportedPackage * ip = parse_imported_path(intrinsics_path, ctx->parser, ctx->hook_registry);
            if (ip)
            {
                ip->is_intrinsics = true; // mark so we don't re-import
                ip->is_using = false;     // intrinsics is referenced qualified (intrinsics.x)
                ip->package_name = strdup("intrinsics");
                ip->analysed = true;

                scope_t * ip_scope = scope_create(NULL, ctx->gen_ctx->context, ctx->gen_ctx->builder);
                ip->package_scope = ip_scope;

                int saved_count = ctx->gen_ctx->count;
                ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = ip_scope;

                char * saved_pkg_name = ctx->package_name;
                ctx->package_name = NULL;
                char const * saved_file_path = ctx->source_file_path;
                ctx->source_file_path = ip->source_path;

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
                ctx->imports[ctx->import_count++] = ip;
                sem_pass1_register_top_level_ex(ctx, ip->ast);
                sem_pass2_analyse_bodies_ast(ctx, ip->ast);

                if (ip->package_name == NULL && ctx->package_name != NULL)
                {
                    free(ip->package_name);
                    ip->package_name = strdup(ctx->package_name);
                }

                ctx->package_name = saved_pkg_name;
                ctx->source_file_path = saved_file_path;
                ctx->gen_ctx->count = saved_count;
            }
            free(intrinsics_path);
        }
    }
}
// Handle a plain import statement (import "path").
static void
sem_pass1_handle_import(SemContext * ctx, odin_grammar_node_t * top_decl)
{
    // ImportSimple children: [StringLiteral("path")]
    if (top_decl->list.count < 1 || top_decl->list.children[0] == NULL)
        return;
    odin_grammar_node_t * path_node = top_decl->list.children[0];
    char * import_path = strip_quotes(path_node->text);
    if (import_path == NULL)
        return;

    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
    free(import_path);
    if (resolved == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
        return;
    }

    if (!import_push_path(ctx, resolved))
    {
        free(resolved);
        return;
    }

    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);

    // import_push_path strdup'd resolved; stack owns it now
    if (pkg == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "failed to parse imported file");
        import_pop_path(ctx);
        free(resolved);
        return;
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
            return;
        }
        ctx->imports = new_arr;
        ctx->import_capacity = new_cap;
    }
    ctx->imports[ctx->import_count++] = pkg;
    pkg->is_direct_import = (ctx->import_reg_depth == 1);

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
// Handle a named import statement (import alias "path").
static void
sem_pass1_handle_import_named(SemContext * ctx, odin_grammar_node_t * top_decl)
{
    if (top_decl->list.count < 2 || top_decl->list.children[0] == NULL
        || top_decl->list.children[0]->text == NULL || top_decl->list.children[1] == NULL)
        return;
    char const * alias_name = top_decl->list.children[0]->text;
    odin_grammar_node_t * path_node = top_decl->list.children[1];
    char * import_path = strip_quotes(path_node->text);
    if (import_path == NULL)
        return;

    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
    free(import_path);
    if (resolved == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
        return;
    }

    if (!import_push_path(ctx, resolved))
    {
        free(resolved);
        return;
    }

    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);
    if (pkg == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "failed to parse imported file");
        import_pop_path(ctx);
        free(resolved);
        return;
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
            return;
        }
        ctx->imports = new_arr;
        ctx->import_capacity = new_cap;
    }
    ctx->imports[ctx->import_count++] = pkg;
    pkg->is_direct_import = (ctx->import_reg_depth == 1);

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

        // Skip semantic analysis for build-ignored packages
        if (!pkg->build_ignored)
        {
            sem_pass1_register_top_level_ex(ctx, pkg->ast);

            sem_pass2_analyse_bodies_ast(ctx, pkg->ast);
        }

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
// Handle a using import statement (import using "path").
static void
sem_pass1_handle_import_using(SemContext * ctx, odin_grammar_node_t * top_decl)
{
    if (top_decl->list.count < 1 || top_decl->list.children[0] == NULL)
        return;
    odin_grammar_node_t * path_node = top_decl->list.children[0];
    char * import_path = strip_quotes(path_node->text);
    if (import_path == NULL)
        return;

    char * resolved = resolve_import_path(import_path, ctx->source_dir, ctx->odin_root);
    free(import_path);
    if (resolved == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "cannot resolve import path");
        return;
    }

    if (!import_push_path(ctx, resolved))
    {
        free(resolved);
        return;
    }

    ImportedPackage * pkg = parse_imported_path(resolved, ctx->parser, ctx->hook_registry);
    if (pkg == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, path_node, "failed to parse imported file");
        import_pop_path(ctx);
        free(resolved);
        return;
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
            return;
        }
        ctx->imports = new_arr;
        ctx->import_capacity = new_cap;
    }
    ctx->imports[ctx->import_count++] = pkg;
    pkg->is_using = true;
    pkg->is_direct_import = (ctx->import_reg_depth == 1);

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
// Register declarations inside a matching when/else branch at top level.
static void
sem_pass1_register_when_decl(SemContext * ctx, odin_grammar_node_t * top_decl)
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

// Register a single top-level declaration during pass 1: package clause,
// imports, constants, variables, foreign blocks, using decls, #assert and
// `when` declarations.
static void
sem_pass1_register_top_level_decl(SemContext * ctx, odin_grammar_node_t * top_decl)
{
    // Handle @require import by unwrapping to the inner import node
    if (top_decl->type == AST_NODE_IMPORT_REQUIRE)
    {
        if (top_decl->list.count < 1 || top_decl->list.children[0] == NULL)
            return;
        top_decl = top_decl->list.children[0];
    }

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
        sem_pass1_handle_import(ctx, top_decl);
    }
    else if (top_decl->type == AST_NODE_IMPORT_NAMED)
    {
        sem_pass1_handle_import_named(ctx, top_decl);
    }
    else if (top_decl->type == AST_NODE_IMPORT_USING)
    {
        sem_pass1_handle_import_using(ctx, top_decl);
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
        sem_pass1_register_when_decl(ctx, top_decl);
    }
}

static void
sem_pass1_register_top_level_ex(SemContext * ctx, odin_grammar_node_t * program_ast)
{
    if (program_ast == NULL)
    {
        return;
    }

    // Only the top-level (main) file may set build_ignored. A build-ignored
    // *import* must not disable compilation of the main file, and nested
    // import pass1 calls run with import_reg_depth >= 1.
    if (ctx->import_reg_depth == 0)
    {
        ctx->build_ignored = ast_file_has_build_ignore(program_ast);
    }
    if (ctx->build_ignored)
    {
        return;
    }

    // Snapshot the pending-bundle list so only bundles deferred while processing
    // THIS file/package are resolved when this call returns (nested import calls
    // resolve their own bundles, but not bundles deferred by the outer file).
    int saved_pending_count = ctx->pending_bundle_count;

    // Phase 1: Track recursion depth so we can mark direct (depth==1) vs
    // transitive imports when they are registered into ctx->imports[].
    ctx->import_reg_depth++;

    sem_pass1_auto_import_runtime(ctx);
    sem_pass1_auto_import_intrinsics(ctx);

    // Copy runtime symbols into the current scope (skip if we are core:runtime itself)
    for (int ri = 0; ri < ctx->import_count; ri++)
    {
        if (ctx->imports[ri] && ctx->imports[ri]->is_runtime && ctx->imports[ri]->package_scope)
        {
            scope_t * cur = generator_current_scope(ctx->gen_ctx);
            if (cur != ctx->imports[ri]->package_scope)
            {
                generic_hash_table_iterate(
                    ctx->imports[ri]->package_scope->symbols.by_name, import_using_copy_symbol, cur
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

                sem_pass1_register_top_level_decl(ctx, top_decl);
            }
        }
    }

    // Phase 1: Restore recursion depth (matches the increment at function entry)
    ctx->import_reg_depth--;

    // Resolve overload bundles that were deferred while processing this file.
    // All top-level declarations (including candidates declared after their
    // bundle) are now registered, so forward references resolve.
    sem_resolve_pending_bundles(ctx, saved_pending_count);
}

static void
sem_pass1_register_top_level(SemContext * ctx)
{
    sem_pass1_register_top_level_ex(ctx, ctx->ast);
}

// --- Pass 2: body analysis dispatcher ---

static void
sem_pass2_analyse_variable_decl(SemContext * ctx, odin_grammar_node_t * node)
{
    {
        if (node->list.count < 1)
            return;
        odin_grammar_node_t * id_list = node->list.children[0];
        if (id_list == NULL || id_list->type != AST_NODE_IDENTIFIER_LIST)
            return;

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
                type_node->resolved_type = var_type;
        }

        if (init_node)
        {
            // Stage 12: When the variable has an explicit declared type
            // (e.g. `r: int = poly_call()`), thread the declared type down
            // to `sem_evaluate_expr` via the poly_expected_return_type
            // context field. This lets `poly_resolve_call` bind `$T` for
            // poly procs that have `$T` in the return position only.
            TypeDescriptor const * prev_expected = ctx->poly_expected_return_type;
            if (type_node != NULL && var_type != NULL)
                ctx->poly_expected_return_type = var_type;

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

            // Always restore the previous expected type (could be NULL).
            ctx->poly_expected_return_type = prev_expected;

            // Tuple destructuring: a, b := some_tuple
            if (id_count > 1 && init_type != NULL && init_type->kind == TD_KIND_TUPLE)
            {
                // First pass: count new vs existing variables.
                // In Odin, := with multiple vars is valid if at least one is new.
                int new_count = 0;
                bool new_flags[256];
                size_t max_vi = id_count < (size_t)init_type->as.tuple.element_count
                                    ? id_count
                                    : (size_t)init_type->as.tuple.element_count;
                for (size_t vi = 0; vi < max_vi; vi++)
                {
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                    {
                        new_flags[vi] = false;
                        continue;
                    }
                    if (strcmp(name_node->text, "_") == 0)
                    {
                        new_flags[vi] = false;
                        continue;
                    }
                    symbol_t * existing = scope_symbols_lookup_entry_by_name(
                        &generator_current_scope(ctx->gen_ctx)->symbols, name_node->text
                    );
                    if (existing == NULL)
                    {
                        new_flags[vi] = true;
                        new_count++;
                    }
                    else
                    {
                        new_flags[vi] = false;
                    }
                }
                if (new_count == 0)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "no new variables on left side of ':='");
                    sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
                    return;
                }
                for (size_t vi = 0; vi < max_vi; vi++)
                {
                    if (!new_flags[vi])
                        continue;
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                        continue;
                    TypedValue tv = create_typed_value(NULL, init_type->as.tuple.element_types[vi], true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
                return;
            }

            // Multi-return destructuring: a, b := foo()
            if (id_count > 1 && init_type != NULL && init_type->kind == TD_KIND_PROC)
            {
                ProcMetadata const * pm = &init_type->proc_metadata;
                // First pass: count new vs existing variables.
                int new_count = 0;
                bool new_flags[256];
                size_t max_vi = id_count < (size_t)pm->return_count ? id_count : (size_t)pm->return_count;
                for (size_t vi = 0; vi < max_vi; vi++)
                {
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                    {
                        new_flags[vi] = false;
                        continue;
                    }
                    if (strcmp(name_node->text, "_") == 0)
                    {
                        new_flags[vi] = false;
                        continue;
                    }
                    symbol_t * existing = scope_symbols_lookup_entry_by_name(
                        &generator_current_scope(ctx->gen_ctx)->symbols, name_node->text
                    );
                    if (existing == NULL)
                    {
                        new_flags[vi] = true;
                        new_count++;
                    }
                    else
                    {
                        new_flags[vi] = false;
                    }
                }
                if (new_count == 0)
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "no new variables on left side of ':='");
                    sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
                    return;
                }
                for (size_t vi = 0; vi < max_vi; vi++)
                {
                    if (!new_flags[vi])
                        continue;
                    odin_grammar_node_t * name_node = id_list->list.children[vi];
                    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                        continue;
                    TypedValue tv = create_typed_value(NULL, pm->returns[vi], true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
                return;
            }
        }

        if (var_type)
        {
            node->resolved_type = var_type;
            if (id_count == 1)
            {
                odin_grammar_node_t * name_node = id_list->list.children[0];
                if (name_node && name_node->type == AST_NODE_IDENTIFIER)
                {
                    // Skip registering _ (blank identifier)
                    if (strcmp(name_node->text, "_") == 0)
                    {
                        return;
                    }
                    // Check for duplicate variable in the same scope:
                    // only for := style (no explicit type node), since top-level
                    // variables with : type are re-processed in pass 2.
                    if (type_node == NULL)
                    {
                        symbol_t * existing = scope_symbols_lookup_entry_by_name(
                            &generator_current_scope(ctx->gen_ctx)->symbols, name_node->text
                        );
                        if (existing != NULL)
                        {
                            char buf[256];
                            snprintf(buf, sizeof(buf), "duplicate variable '%s' in the same scope", name_node->text);
                            sem_error_list_add(&ctx->errors, ctx->source_file_path, name_node, buf);
                            return;
                        }
                    }
                    TypedValue tv = create_typed_value(NULL, var_type, true);
                    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                }
            }
        }
        return;
    }
}

static void
sem_pass2_analyse_constant_decl(SemContext * ctx, odin_grammar_node_t * node)
{
    {
        if (node->list.count < 2)
            return;
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
            return;
        if (value_node == NULL)
            return;

        if (value_node->type == AST_NODE_PROCEDURE_DEFINITION)
        {
            sem_analyse_procedure_literal(ctx, value_node, name_node->text);
        }

        // Set LLVM mangled name for all imported package symbols.
        // Main package symbols keep bare names so the entry point
        // wrapper can find `main`.
        if (ctx->import_stack_count > 0 && ctx->package_name != NULL)
        {
            ProcDeclAttributes * attrs = (ProcDeclAttributes *)node->metadata;
            // Don't mangle @(builtin) intrinsics — they dispatch by
            // bare name in ir_gen_runtime_intrinsic_body.
            if (attrs == NULL || !attrs->is_builtin)
            {
                char mangled[512];
                int len = snprintf(mangled, sizeof(mangled), "%s.%s", ctx->package_name, name_node->text);
                if (len > 0 && len < (int)sizeof(mangled))
                {
                    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                    if (sym)
                        sym->llvm_name = strdup(mangled);
                }
            }
        }

        if (value_node->type == AST_NODE_STRUCT_TYPE)
        {
            // Check if this is a poly struct type — skip resolution
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym && sym->is_polymorphic)
            {
                // Poly struct template — skip resolution. Type will be
                // instantiated at usage sites via TypeApplication.
                return;
            }
            // Non-poly struct: resolve normally
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, value_node);
            if (td != NULL)
            {
                value_node->resolved_type = td;
                TypedValue tv = create_typed_value(NULL, td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                symbol_t * s = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (s)
                    s->kind = SYMBOL_TYPE;
                return;
            }
            sem_evaluate_expr(ctx, value_node);
        }
        else if (value_node->type == AST_NODE_ENUM_TYPE)
        {
            // Check if this is a poly enum type — skip resolution
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym && sym->is_polymorphic)
            {
                // Poly enum template — skip resolution. Type will be
                // instantiated at usage sites via TypeApplication.
                return;
            }
            // Non-poly enum: resolve normally
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, value_node);
            if (td != NULL)
            {
                value_node->resolved_type = td;
                TypedValue tv = create_typed_value(NULL, td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                symbol_t * s = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (s)
                    s->kind = SYMBOL_TYPE;
                return;
            }
            sem_evaluate_expr(ctx, value_node);
        }
        else if (value_node->type == AST_NODE_UNION_TYPE)
        {
            // Check if this is a poly union type — skip resolution
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            if (sym && sym->is_polymorphic)
            {
                // Poly union template — skip resolution. Type will be
                // instantiated at usage sites via TypeApplication.
                return;
            }
            // Non-poly union: resolve normally
            TypeDescriptor const * td = sem_resolve_type_expr(ctx, value_node);
            if (td != NULL)
            {
                value_node->resolved_type = td;
                TypedValue tv = create_typed_value(NULL, td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                symbol_t * s = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (s)
                    s->kind = SYMBOL_TYPE;
                return;
            }
            sem_evaluate_expr(ctx, value_node);
        }
        else if (value_node->type == AST_NODE_PROC_OVERLOAD_BUNDLE)
        {
            int candidate_count = (int)value_node->list.count;
            if (candidate_count > 0)
            {
                TypeDescriptor const ** candidate_types
                    = (TypeDescriptor const **)malloc((size_t)candidate_count * sizeof(TypeDescriptor const *));
                symbol_t ** candidate_symbols = (symbol_t **)malloc((size_t)candidate_count * sizeof(symbol_t *));
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
                    else if (sym && sym->is_polymorphic)
                    {
                        candidate_types[valid_count] = NULL;
                        candidate_symbols[valid_count] = sym;
                        valid_count++;
                    }
                    else
                    {
                        char buf[256];
                        snprintf(
                            buf, sizeof(buf), "candidate '%s' in overload bundle is not a procedure", id_node->text
                        );
                        sem_error_list_add(&ctx->errors, NULL, id_node, buf);
                    }
                }
                if (valid_count > 0)
                {
                    TypeDescriptor const * bundle_type = get_or_create_overload_bundle_type(
                        ctx->type_registry, candidate_types, candidate_symbols, valid_count
                    );
                    value_node->resolved_type = bundle_type;
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
                value_node->resolved_type = td;
                TypedValue tv = create_typed_value(NULL, td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
                if (sym)
                    sym->kind = SYMBOL_TYPE;
                return;
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

        // Intrinsic-alias detection: `@private IS_FLOAT :: intrinsics.type_is_float`
        // registers IS_FLOAT -> type_is_float so where clauses can use IS_FLOAT(T).
        {
            symbol_t * alias_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
            symbol_t * intrinsic_sym = sem_find_intrinsic_from_value(value_node);
            if (alias_sym != NULL && intrinsic_sym != NULL)
                poly_register_intrinsic_alias(alias_sym, intrinsic_sym);
        }

        // Error: main proc must not have a return type; use os.exit() to set exit codes
        if (value_node->type == AST_NODE_PROCEDURE_DEFINITION && strcmp(name_node->text, "main") == 0)
        {
            if (val_type != NULL && val_type->kind == TD_KIND_PROC && val_type->proc_metadata.return_count > 0
                && val_type->proc_metadata.return_type != NULL
                && val_type->proc_metadata.return_type != type_descriptor_get_void_type(ctx->type_registry))
            {
                sem_error_list_add(
                    &ctx->errors,
                    NULL,
                    name_node,
                    "main procedure must not return a value; use os.exit() to set exit codes"
                );
            }
        }
        return;
    }
}

static void
sem_pass2_analyse_when_statement(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type
)
{
    {
        // Flat structure: [cond1, body1, cond2, body2, ..., elseBody?]
        // Conditions are evaluated at compile time via the poly evaluator
        // (handles $N poly ints, $T poly types, intrinsics predicates, etc.).
        // Only the first matching branch is analysed; the selection is stored
        // in node->metadata for the IR generator.
        odin_grammar_node_t * selected = NULL;
        size_t n = node->list.count;
        size_t i = 0;
        while (i + 1 < n)
        {
            odin_grammar_node_t * cond = node->list.children[i];
            odin_grammar_node_t * body = node->list.children[i + 1];
            if (cond == NULL || body == NULL || body->type != AST_NODE_COMPOUND_STATEMENT)
                break;
            long long val = poly_eval_where_expr(ctx, cond);
            if (val == -1)
            {
                sem_error_list_add(
                    &ctx->errors, ctx->source_file_path, cond, "when condition must evaluate to a compile-time constant"
                );
                break;
            }
            if (val != 0)
            {
                selected = body;
                break;
            }
            i += 2;
        }

        // Trailing `else` body (odd remaining child)
        if (selected == NULL && i + 1 == n && node->list.children[i] != NULL
            && node->list.children[i]->type == AST_NODE_COMPOUND_STATEMENT)
        {
            selected = node->list.children[i];
        }

        // No matching branch and no else: the statement generates nothing.
        if (selected == NULL)
        {
            poly_register_when_selection(node, NULL);
            return;
        }

        poly_register_when_selection(node, selected);
        generator_push_scope(ctx->gen_ctx);
        sem_analyse_compound_statement(ctx, selected, expected_return_type);
        generator_pop_scope(ctx->gen_ctx);
        return;
    }
}

static void
sem_pass2_analyse_if_statement(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type
)
{
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
            else if (child->type == AST_NODE_EXPRESSION_STATEMENT || child->type == AST_NODE_ASSIGN_STATEMENT
                     || child->type == AST_NODE_VARIABLE_DECL || child->type == AST_NODE_CONSTANT_DECL
                     || child->type == AST_NODE_RETURN_STATEMENT || child->type == AST_NODE_BREAK_STATEMENT
                     || child->type == AST_NODE_CONTINUE_STATEMENT || child->type == AST_NODE_DEFER_STATEMENT
                     || child->type == AST_NODE_FALLTHROUGH_STATEMENT || child->type == AST_NODE_FOR_STATEMENT
                     || child->type == AST_NODE_SWITCH_STATEMENT || child->type == AST_NODE_WHEN_STATEMENT)
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
        return;
    }
}

static void
sem_pass2_analyse_for_statement(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type
)
{
    odin_grammar_node_t * body = node_find_child(node, AST_NODE_COMPOUND_STATEMENT);

    // Skip a leading `#unroll` directive child (Directive? in ForStatement)
    size_t start_idx = 0;
    if (node->list.count > 0 && node->list.children[0] != NULL && node->list.children[0]->type == AST_NODE_DIRECTIVE)
    {
        start_idx = 1;
    }

    // Detect for-range: first child is a raw Identifier
    bool is_for_range = false;
    if (node->list.count >= 2 && node->list.children[start_idx] != NULL
        && node->list.children[start_idx]->type == AST_NODE_IDENTIFIER)
    {
        for (size_t i = start_idx + 1; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
                break;
            if (child->type == AST_NODE_IDENTIFIER)
                continue;
            sem_evaluate_expr(ctx, child);
            if (child->resolved_type
                && (child->resolved_type->kind == TD_KIND_RANGE || child->resolved_type->kind == TD_KIND_VECTOR
                    || child->resolved_type->kind == TD_KIND_ARRAY))
            {
                is_for_range = true;
            }
            break;
        }
    }

    // For non-range for loops, evaluate condition expressions before pushing scope
    if (!is_for_range && node->list.count >= 1 && node->list.children[start_idx] != NULL
        && node->list.children[start_idx]->type != AST_NODE_COMPOUND_STATEMENT)
    {
        sem_evaluate_expr(ctx, node->list.children[start_idx]);
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
    else
    {
        // For the `do`-form (for v in arr do stmt), find the body statement
        // as the last non-range, non-identifier child.
        odin_grammar_node_t * body_stmt = NULL;
        for (size_t i = node->list.count; i > 0; i--)
        {
            odin_grammar_node_t * child = node->list.children[i - 1];
            if (child == NULL)
                continue;
            if (is_for_range && child->type == AST_NODE_IDENTIFIER)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
                continue;
            body_stmt = child;
            break;
        }
        if (body_stmt)
            sem_pass2_node(ctx, body_stmt, expected_return_type);
    }

    generator_pop_scope(ctx->gen_ctx);
}

static void
sem_pass2_analyse_switch_statement(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type
)
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
    bool can_check_exhaustiveness
        = (switch_type != NULL && switch_type->kind == TD_KIND_ENUM && !has_default && !is_partial
           && switch_type->as.enum_type.enumerator_count > 0 && switch_type->as.enum_type.enumerator_values != NULL);

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
                    || case_child->type == AST_NODE_BREAK_STATEMENT || case_child->type == AST_NODE_CONTINUE_STATEMENT
                    || case_child->type == AST_NODE_FALLTHROUGH_STATEMENT
                    || case_child->type == AST_NODE_EXPRESSION_STATEMENT
                    || case_child->type == AST_NODE_ASSIGN_STATEMENT || case_child->type == AST_NODE_VARIABLE_DECL
                    || case_child->type == AST_NODE_IF_STATEMENT || case_child->type == AST_NODE_FOR_STATEMENT
                    || case_child->type == AST_NODE_SWITCH_STATEMENT || case_child->type == AST_NODE_DEFER_STATEMENT)
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
                        while (ident != NULL && ident->type != AST_NODE_IDENTIFIER && ident->list.count > 0)
                        {
                            ident = ident->list.children[0];
                        }
                        // Try to get the constant int value of the case expression
                        if (ident != NULL && ident->type == AST_NODE_IDENTIFIER && ident->text != NULL)
                        {
                            symbol_t * sym
                                = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
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
                snprintf(
                    buf,
                    sizeof(buf),
                    "switch is not exhaustive: missing case for enum value '%s'",
                    enum_names[ei] ? enum_names[ei] : "<unknown>"
                );
                sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
            }
        }
    }
}

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
        sem_pass2_analyse_variable_decl(ctx, node);
        break;

    case AST_NODE_CONSTANT_DECL:
        sem_pass2_analyse_constant_decl(ctx, node);
        break;

    case AST_NODE_WHEN_STATEMENT:
        sem_pass2_analyse_when_statement(ctx, node, expected_return_type);
        break;

    case AST_NODE_IF_STATEMENT:
        sem_pass2_analyse_if_statement(ctx, node, expected_return_type);
        break;

    case AST_NODE_FOR_STATEMENT:
        sem_pass2_analyse_for_statement(ctx, node, expected_return_type);
        break;

    case AST_NODE_SWITCH_STATEMENT:
        sem_pass2_analyse_switch_statement(ctx, node, expected_return_type);
        break;

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

// Phase 1 import-usage tracking (using imports):
// Walk the main file's AST looking for AST_NODE_IDENTIFIER nodes with
// resolved_symbol set. For each, check whether the resolved symbol matches
// a symbol in any `import using` package's scope (by name + type_info ptr)
// and mark such packages as is_used=true.
//
// Non-using imports are marked is_used=true at the point of detection in
// sem_evaluate_postfix_expr (package-qualified reference).
static void
sem_track_using_import_usage_visitor(odin_grammar_node_t * node, SemContext * ctx)
{
    if (node == NULL || node->type != AST_NODE_IDENTIFIER)
        return;
    if (node->resolved_symbol == NULL || node->text == NULL)
        return;

    symbol_t const * resolved = node->resolved_symbol;

    for (int i = 0; i < ctx->import_count; i++)
    {
        ImportedPackage * pkg = ctx->imports[i];
        if (pkg == NULL || !pkg->is_using || pkg->package_scope == NULL)
            continue;
        if (pkg->is_used)
            continue; // already marked; skip the lookup
        if (!pkg->is_direct_import)
            continue; // transitive using-imports are always kept (default true)

        symbol_t * pkg_sym = scope_find_symbol_entry(pkg->package_scope, node->text);
        if (pkg_sym == NULL)
            continue;

        // To avoid false positives from using-imports that copy symbols from
        // other packages into their scope, also verify the symbol's llvm_name
        // matches the package name prefix. For a symbol copied from package A
        // to package B, the llvm_name would be "package_a.symbol", not
        // "package_b.symbol".
        bool name_matches = false;
        if (pkg_sym->llvm_name != NULL && resolved->llvm_name != NULL)
        {
            // Check if the llvm_name starts with the package's name
            size_t pkg_name_len = strlen(pkg->package_name);
            size_t llvm_name_len = strlen(pkg_sym->llvm_name);
            name_matches
                = (llvm_name_len > pkg_name_len + 1 // +1 for '.'
                   && strncmp(pkg_sym->llvm_name, pkg->package_name, pkg_name_len) == 0
                   && pkg_sym->llvm_name[pkg_name_len] == '.');
        }

        // Match by name + type_info pointer. The TypedValue copy preserves
        // the type_info pointer from the source scope, so a reference to a
        // copied using-import symbol will have the same type_info as the
        // source package scope's symbol.
        if (pkg_sym->value.type_info != NULL && pkg_sym->value.type_info == resolved->value.type_info && name_matches)
        {
            pkg->is_used = true;
            return; // no need to check other using imports for this node
        }

        // Fall back: when type_info is NULL (e.g. untyped constants), match
        // by name alone. This may over-mark (false positives) but never
        // under-mark for using imports.
        if (pkg_sym->value.type_info == NULL && resolved->value.type_info == NULL && pkg_sym->name != NULL
            && strcmp(pkg_sym->name, node->text) == 0 && name_matches)
        {
            pkg->is_used = true;
            return;
        }
    }
}

static void
sem_walk_ast_track_usage(odin_grammar_node_t * node, SemContext * ctx)
{
    if (node == NULL)
        return;
    sem_track_using_import_usage_visitor(node, ctx);
    for (size_t i = 0; i < node->list.count; i++)
        sem_walk_ast_track_usage(node->list.children[i], ctx);
}

// After pass 2, walk the main file's AST to mark using imports as used
// based on identifier references to their copied symbols.
static void
sem_track_using_import_usage(SemContext * ctx)
{
    sem_walk_ast_track_usage(ctx->ast, ctx);
}

bool
sem_analyse(SemContext * ctx)
{
    sem_pass1_register_top_level(ctx);
    if (sem_error_list_has_errors(&ctx->errors))
        return false;

    // Skip semantic analysis if build-ignored
    if (ctx->build_ignored)
        return true;

    sem_pass2_analyse_bodies_ast(ctx, ctx->ast);
    if (sem_error_list_has_errors(&ctx->errors))
        return false;

    // Phase 1: After pass 2, mark using imports as used based on identifier
    // references in the main file's AST. Non-using imports are marked inline
    // in sem_evaluate_postfix_expr (package-qualified references).
    sem_track_using_import_usage(ctx);

    // Phase 1: Mark transitive imports as used. These are imports that are
    // NOT direct imports of the main file (e.g., imports of imports).
    // They are needed by their parent package, so we must codegen them.
    //
    // NOTE: This is conservative - transitive imports of unused packages
    // will still be codegen'd, but LLVM's DCE will remove them.
    // A more precise implementation would track parent-child relationships.
    for (int i = 0; i < ctx->import_count; i++)
    {
        ImportedPackage * pkg = ctx->imports[i];
        if (pkg == NULL)
            continue;
        if (!pkg->is_direct_import)
            pkg->is_used = true;
    }

    return true;
}

#include <stdio.h>
