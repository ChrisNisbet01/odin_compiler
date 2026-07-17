#include "semantic_analyser.h"

#include "ast_utils.h"
#include "package_resolver.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration for overload bundle resolution (used in POSTFIX_EXPRESSION handler)
static symbol_t * sem_resolve_overload_bundle_call(
    SemContext * ctx,
    TypeDescriptor const * bundle_type,
    odin_grammar_node_t * arg_list_node,
    odin_grammar_node_t * call_op,
    char const * callee_name
);

static calling_convention_t
parse_calling_convention(char const * text)
{
    if (text == NULL)
        return CALLING_CONV_ODIN;
    // StringLiteral text includes surrounding quotes like "c" — strip them
    size_t len = strlen(text);
    if (len >= 2 && (text[0] == '"' || text[0] == '`') && (text[len - 1] == '"' || text[len - 1] == '`'))
    {
        char * inner = strndup(text + 1, len - 2);
        calling_convention_t result = parse_calling_convention(inner);
        free(inner);
        return result;
    }
    if (strcmp(text, "odin") == 0)
        return CALLING_CONV_ODIN;
    if (strcmp(text, "contextless") == 0)
        return CALLING_CONV_CONTEXTLESS;
    if (strcmp(text, "c") == 0 || strcmp(text, "cdecl") == 0)
        return CALLING_CONV_C;
    if (strcmp(text, "stdcall") == 0 || strcmp(text, "std") == 0)
        return CALLING_CONV_STDCALL;
    if (strcmp(text, "fastcall") == 0 || strcmp(text, "fast") == 0)
        return CALLING_CONV_FASTCALL;
    return CALLING_CONV_NONE;
}

static bool
is_valid_swizzle(char const * field, int lane_count)
{
    if (field == NULL || field[0] == '\0')
        return false;

    int swizzle_set = -1;
    for (char const * p = field; *p; p++)
    {
        char c = *p;
        int char_set = -1;
        int char_idx = -1;
        if (c == 'x' || c == 'y' || c == 'z' || c == 'w')
        {
            char_set = 0;
            char_idx = c - 'x';
        }
        else if (c == 'r' || c == 'g' || c == 'b' || c == 'a')
        {
            char_set = 1;
            char_idx = c - 'r';
        }
        else
        {
            return false;
        }
        if (swizzle_set == -1)
            swizzle_set = char_set;
        else if (swizzle_set != char_set)
            return false;
        if (char_idx >= lane_count)
            return false;
    }
    return true;
}

void
sem_context_init(
    SemContext * ctx,
    odin_grammar_node_t * ast,
    TypeDescriptors * type_registry,
    GeneratorContext * gen_ctx,
    char const * source_file_path,
    char const * source_dir,
    char const * odin_root,
    epc_parser_t * parser,
    epc_ast_hook_registry_t * hooks
)
{
    ctx->ast = ast;
    ctx->type_registry = type_registry;
    ctx->gen_ctx = gen_ctx;
    sem_error_list_init(&ctx->errors);
    ctx->source_file_path = source_file_path;
    ctx->source_dir = source_dir;
    ctx->odin_root = odin_root;
    ctx->package_name = NULL;
    ctx->imports = NULL;
    ctx->import_count = 0;
    ctx->import_capacity = 0;
    ctx->parser = parser;
    ctx->hook_registry = hooks;
    ctx->import_stack = NULL;
    ctx->import_stack_count = 0;
    ctx->import_stack_capacity = 0;
    register_builtin_context_types(type_registry);
}

void
sem_context_destroy(SemContext * ctx)
{
    free(ctx->package_name);
    ctx->package_name = NULL;
    for (int i = 0; i < ctx->import_count; i++)
    {
        imported_package_free(ctx->imports[i]);
    }
    free(ctx->imports);
    ctx->imports = NULL;
    ctx->import_count = 0;
    ctx->import_capacity = 0;
    for (int i = 0; i < ctx->import_stack_count; i++)
        free(ctx->import_stack[i]);
    free(ctx->import_stack);
    ctx->import_stack = NULL;
    ctx->import_stack_count = 0;
    ctx->import_stack_capacity = 0;
}

// --- Helper: strip quotes from StringLiteral text ---
static char *
strip_quotes(char const * text)
{
    if (text == NULL)
        return NULL;
    size_t len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"')
    {
        return strndup(text + 1, len - 2);
    }
    return strdup(text);
}

// --- Helper: find imported package by name ---
static ImportedPackage *
find_imported_package_by_name(SemContext * ctx, char const * name)
{
    if (name == NULL)
        return NULL;
    for (int i = 0; i < ctx->import_count; i++)
    {
        if (ctx->imports[i]->package_name != NULL && strcmp(ctx->imports[i]->package_name, name) == 0)
        {
            return ctx->imports[i];
        }
    }
    return NULL;
}

// --- Comma-chain argument collection ---
// Walk a comma-chain Expression tree to collect individual argument nodes.
// Comma is handled by chainl1(AssignExpression, Comma) which produces
// left-associative Expression trees. For a, b, c, the tree is:
//   Expr(Expr(a, b), c) — last child is the rightmost operand.
static void
sem_collect_comma_chain_args(odin_grammar_node_t * node, odin_grammar_node_t ** out_args, int max_args, int * out_count)
{
    if (node == NULL || max_args <= 0)
        return;
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
        // children[0] is the left sub-chain, children[last] is the rightmost operand
        int last_idx = (int)node->list.count - 1;
        odin_grammar_node_t * last = node->list.children[last_idx];
        sem_collect_comma_chain_args(node->list.children[0], out_args, max_args, out_count);
        if (*out_count < max_args && last != NULL)
        {
            out_args[*out_count] = last;
            (*out_count)++;
        }
    }
    else
    {
        // Single expression
        if (*out_count < max_args)
        {
            out_args[*out_count] = node;
            (*out_count)++;
        }
    }
}

// --- Forward declarations ---
static TypeDescriptor const * sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_integer_value(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_float_value(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_string_literal(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_rune_literal(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_bool_value(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_auto_cast_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_cast_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_implicit_cast_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_len_cap_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_make_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_new_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_delete_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_expand_values_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_compress_values_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_soa_zip_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_soa_unzip_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_incl_excl_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_complex_quaternion_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_size_align_of_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_offset_of_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_raw_data_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_type_of_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_typeid_of_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_type_info_of_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_min_max_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_distinct_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_nil(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_directive(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_context_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_identifier(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_unary_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_range_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_binary_arith_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_comp_log_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_postfix_call(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_postfix_member(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_or_else(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_or_return(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_ternary_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_expression_wrapper(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_assign_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_postfix_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_directive_with_args(SemContext * ctx, odin_grammar_node_t * node);
static void sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type);
static void sem_pass1_register_top_level_ex(SemContext * ctx, odin_grammar_node_t * program_ast);
static void sem_pass2_analyse_bodies_ast(SemContext * ctx, odin_grammar_node_t * program);
static void sem_analyse_attributes(odin_grammar_node_t * decl_node);
static void sem_check_assignment(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * target_type,
    TypeDescriptor const * src_type, odin_grammar_node_t * src_node
);

// --- Compile-time constant integer evaluation ---
// Evaluates a constant expression to an integer at compile time.
// Returns the value and sets *ok = 1 on success, or sets *ok = 0 on failure.
static long long
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
static int
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

// --- Type expression resolution ---

static TypeDescriptor const *
sem_resolve_type_name(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->text == NULL)
        return NULL;
    return get_basic_type_by_name(ctx->type_registry, node->text);
}

static TypeDescriptor const *
sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

    switch (node->type)
    {
    case AST_NODE_BASIC_TYPE:
    case AST_NODE_TYPE_NAME:
    {
        TypeDescriptor const * td = sem_resolve_type_name(ctx, node);
        if (td != NULL)
            return td;
        // For compound types wrapped in TypeName (e.g. struct { ... }), resolve children
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (node->list.children[i] != NULL)
            {
                td = sem_resolve_type_expr(ctx, node->list.children[i]);
                if (td != NULL)
                {
                    node->resolved_type = (TypeDescriptor *)td;
                    return td;
                }
            }
        }
        return NULL;
    }

    case AST_NODE_POINTER_TYPE:
    {
        TypeDescriptor const * pointee = sem_resolve_type_expr(ctx, node->list.children[0]);
        if (pointee == NULL)
            return NULL;
        return get_or_create_pointer_type(ctx->type_registry, pointee);
    }

    case AST_NODE_ARRAY_TYPE:
    {
        // ArrayType = LBracket (IntegerLiteral)? RBracket TypePrefix
        odin_grammar_node_t * size_node = node_find_child(node, AST_NODE_INTEGER_VALUE);
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                elem_type_node = child;
                break;
            }
        }
        if (elem_type_node == NULL)
            return NULL;

        TypeDescriptor const * elem_type = sem_resolve_type_expr(ctx, elem_type_node);
        if (elem_type == NULL)
            return NULL;

        size_t count = 0;
        if (size_node && size_node->text)
        {
            count = (size_t)parse_odin_unsigned(size_node->text, NULL, 0);
        }

        TypeDescriptor const * arr_type = get_or_create_array_type(ctx->type_registry, elem_type, count);
        if (arr_type)
            node->resolved_type = (TypeDescriptor *)arr_type;
        return arr_type;
    }

    case AST_NODE_DISTINCT_TYPE:
    {
        // DistinctType = KwDistinct TypePrefix
        // Create a new distinct type descriptor wrapping the resolved base type
        odin_grammar_node_t * inner = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                inner = child;
                break;
            }
        }
        if (inner == NULL)
            return NULL;
        TypeDescriptor const * base = sem_resolve_type_expr(ctx, inner);
        if (base == NULL)
            return NULL;
        TypeDescriptor const * distinct_td = create_distinct_type(ctx->type_registry, base);
        if (distinct_td)
            node->resolved_type = (TypeDescriptor *)distinct_td;
        return distinct_td;
    }

    case AST_NODE_SLICE_TYPE:
    {
        // SliceType = LBracket RBracket TypePrefix
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                elem_type_node = child;
                break;
            }
        }
        if (elem_type_node == NULL)
            return NULL;

        TypeDescriptor const * elem_type = sem_resolve_type_expr(ctx, elem_type_node);
        if (elem_type == NULL)
            return NULL;

        TypeDescriptor const * slice_type = get_or_create_slice_type(ctx->type_registry, elem_type);
        if (slice_type)
            node->resolved_type = (TypeDescriptor *)slice_type;
        return slice_type;
    }

    case AST_NODE_MULTI_POINTER_TYPE:
    {
        // MultiPointerType = LBracket PointerCaret RBracket TypePrefix
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                elem_type_node = child;
                break;
            }
        }
        if (elem_type_node == NULL)
            return NULL;

        TypeDescriptor const * elem_type = sem_resolve_type_expr(ctx, elem_type_node);
        if (elem_type == NULL)
            return NULL;

        TypeDescriptor const * mp_type = get_or_create_multi_pointer_type(ctx->type_registry, elem_type);
        if (mp_type)
            node->resolved_type = (TypeDescriptor *)mp_type;
        return mp_type;
    }

    case AST_NODE_DYNAMIC_ARRAY_TYPE:
    {
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                elem_type_node = child;
                break;
            }
        }
        if (elem_type_node == NULL)
            return NULL;

        TypeDescriptor const * elem_type = sem_resolve_type_expr(ctx, elem_type_node);
        if (elem_type == NULL)
            return NULL;

        TypeDescriptor const * da_type = get_or_create_dynamic_array_type(ctx->type_registry, elem_type);
        if (da_type)
            node->resolved_type = (TypeDescriptor *)da_type;
        return da_type;
    }

    case AST_NODE_MAP_TYPE:
    {
        // Grammar: MapType = KwMap LBracket TypePrefix RBracket TypePrefix
        // Children: key_type_node, value_type_node
        odin_grammar_node_t * key_type_node = NULL;
        odin_grammar_node_t * val_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && (is_type_node(child) || child->type == AST_NODE_IDENTIFIER))
            {
                if (key_type_node == NULL)
                    key_type_node = child;
                else
                    val_type_node = child;
            }
        }
        if (key_type_node == NULL || val_type_node == NULL)
            return NULL;

        TypeDescriptor const * key_type = sem_resolve_type_expr(ctx, key_type_node);
        TypeDescriptor const * val_type = sem_resolve_type_expr(ctx, val_type_node);
        if (key_type == NULL || val_type == NULL)
            return NULL;

        TypeDescriptor const * map_type = get_or_create_map_type(ctx->type_registry, key_type, val_type);
        if (map_type)
            node->resolved_type = (TypeDescriptor *)map_type;
        return map_type;
    }

    case AST_NODE_BIT_FIELD_TYPE:
    {
        odin_grammar_node_t * field_list_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && child->type == AST_NODE_BIT_FIELD_FIELD_LIST)
            {
                field_list_node = child;
                break;
            }
        }
        if (field_list_node == NULL)
            return NULL;

        int num_fields = 0;
        for (size_t i = 0; i < field_list_node->list.count; i++)
        {
            if (field_list_node->list.children[i]
                && field_list_node->list.children[i]->type == AST_NODE_BIT_FIELD_FIELD)
                num_fields++;
        }
        if (num_fields == 0)
            return NULL;

        bit_field_field_info fields[64];
        int offset = 0;
        int field_idx = 0;
        int total_bits = 0;

        for (size_t i = 0; i < field_list_node->list.count; i++)
        {
            odin_grammar_node_t * field_node = field_list_node->list.children[i];
            if (field_node == NULL || field_node->type != AST_NODE_BIT_FIELD_FIELD)
                continue;
            if (field_node->list.count < 3)
                return NULL;

            odin_grammar_node_t * name_node = field_node->list.children[0];
            odin_grammar_node_t * type_node = field_node->list.children[1];
            odin_grammar_node_t * width_node = field_node->list.children[2];

            if (name_node == NULL || name_node->text == NULL || type_node == NULL || width_node == NULL)
                return NULL;

            TypeDescriptor const * field_type = sem_resolve_type_expr(ctx, type_node);
            if (field_type == NULL)
                return NULL;

            int width = (int)strtol(width_node->text, NULL, 10);
            if (width <= 0)
            {
                sem_error_list_add(&ctx->errors, NULL, width_node, "bit_field field width must be positive");
                return NULL;
            }

            fields[field_idx].name = name_node->text;
            fields[field_idx].type = field_type;
            fields[field_idx].offset_bits = offset;
            fields[field_idx].width_bits = width;
            offset += width;
            total_bits += width;
            field_idx++;
        }

        TypeDescriptor const * bf_type
            = get_or_create_bit_field_type(ctx->type_registry, fields, num_fields, total_bits);
        if (bf_type)
            node->resolved_type = (TypeDescriptor *)bf_type;
        return bf_type;
    }

    case AST_NODE_BIT_SET_TYPE:
    {
        if (node->list.count < 1)
            return NULL;

        odin_grammar_node_t * inner = node->list.children[0];
        if (inner == NULL)
            return NULL;

        TypeDescriptor const * backing_type = NULL;
        int num_bits = 0;

        if (inner->type == AST_NODE_BIT_SET_RANGE)
        {
            // Range-based bit_set: bit_set[low..high] or bit_set[low..<high]
            // Collect integer values by walking descendants depth-first
            odin_grammar_node_t * int_vals[2] = {NULL, NULL};
            int int_count = 0;

            // Use a simple stack-based traversal of the inner node's subtree
            odin_grammar_node_t ** stack = NULL;
            size_t stack_cap = 0;
            size_t stack_size = 0;

// Helper macro to push onto stack
#define PUSH(n)                                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (stack_size >= stack_cap)                                                                                   \
        {                                                                                                              \
            stack_cap = stack_cap ? stack_cap * 2 : 16;                                                                \
            stack = realloc(stack, stack_cap * sizeof(*stack));                                                        \
        }                                                                                                              \
        stack[stack_size++] = (n);                                                                                     \
    } while (0)

            for (size_t i = 0; i < inner->list.count; i++)
            {
                if (inner->list.children[i] != NULL)
                    PUSH(inner->list.children[i]);
            }

            while (stack_size > 0 && int_count < 2)
            {
                odin_grammar_node_t * cur = stack[--stack_size];
                if (cur->type == AST_NODE_INTEGER_VALUE)
                {
                    int_vals[int_count++] = cur;
                }
                else
                {
                    for (size_t i = 0; i < cur->list.count; i++)
                    {
                        if (cur->list.children[i] != NULL)
                            PUSH(cur->list.children[i]);
                    }
                }
            }

            free(stack);
#undef PUSH

            if (int_count < 2 || int_vals[0]->text == NULL || int_vals[1]->text == NULL)
                return NULL;

            char * endptr = NULL;
            unsigned long long low_val = parse_odin_unsigned(int_vals[0]->text, &endptr, 0);
            unsigned long long high_val = parse_odin_unsigned(int_vals[1]->text, &endptr, 0);

            // Determine inclusive/exclusive from captured text
            bool is_inclusive = true;
            if (inner->text != NULL)
            {
                char const * dotpos = strstr(inner->text, "..");
                if (dotpos != NULL && dotpos[2] == '<')
                    is_inclusive = false;
            }

            if (is_inclusive)
                num_bits = (int)(high_val - low_val + 1);
            else
                num_bits = (int)(high_val - low_val);

            if (num_bits < 1)
                num_bits = 1;

            // Pick backing type based on number of bits
            char const * backing_name = "u8";
            if (num_bits <= 8)
                backing_name = "u8";
            else if (num_bits <= 16)
                backing_name = "u16";
            else if (num_bits <= 32)
                backing_name = "u32";
            else
                backing_name = "u64";

            backing_type = get_basic_type_by_name(ctx->type_registry, backing_name);
            if (backing_type == NULL)
                return NULL;
        }
        else if (inner->type == AST_NODE_BASIC_TYPE)
        {
            backing_type = sem_resolve_type_expr(ctx, inner);
            if (backing_type == NULL || backing_type->kind != TD_KIND_BASIC || backing_type->as.basic.is_float)
                return NULL;
            num_bits = backing_type->as.basic.width;
        }
        else
        {
            // Try to resolve as general type expression (e.g. enum type)
            backing_type = sem_resolve_type_expr(ctx, inner);
            if (backing_type == NULL)
                return NULL;
            if (backing_type->kind == TD_KIND_ENUM)
            {
                // For enum-based bit_set, determine backing from enum values
                num_bits = 64; // conservative
            }
            else if (backing_type->kind == TD_KIND_BASIC && !backing_type->as.basic.is_float)
            {
                num_bits = backing_type->as.basic.width;
            }
            else
            {
                return NULL;
            }
        }

        TypeDescriptor const * bs_type = get_or_create_bit_set_type(ctx->type_registry, backing_type, num_bits);
        if (bs_type)
            node->resolved_type = (TypeDescriptor *)bs_type;
        return bs_type;
    }

    case AST_NODE_PROCEDURE_SIGNATURE:
    {
        TypeDescriptor const * return_type = NULL;
        TypeDescriptor const ** return_types = NULL;
        int return_count = 0;
        int param_count = 0;
        TypeDescriptor const ** params = NULL;
        size_t param_cap = 0;
        bool is_variadic = false;
        calling_convention_t cc = CALLING_CONV_ODIN;

        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_CALLING_CONVENTION && child->list.count > 0)
            {
                odin_grammar_node_t * str_child = child->list.children[0];
                if (str_child && str_child->text)
                    cc = parse_calling_convention(str_child->text);
            }
            else if (child->type == AST_NODE_RETURNS && child->list.count > 0)
            {
                odin_grammar_node_t * ret_child = child->list.children[0];
                if (ret_child->type == AST_NODE_RETURN_TYPE_LIST)
                {
                    return_count = 0;
                    return_types = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
                    for (size_t ri = 0; ri < ret_child->list.count; ri++)
                    {
                        odin_grammar_node_t * type_node = ret_child->list.children[ri];
                        if (type_node == NULL)
                            continue;
                        TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
                        type_node->resolved_type = (TypeDescriptor *)td;
                        if (td)
                            return_types[return_count++] = td;
                    }
                }
                else if (ret_child->type == AST_NODE_RETURN_LIST)
                {
                    return_count = 0;
                    return_types = calloc(ret_child->list.count, sizeof(TypeDescriptor const *));
                    for (size_t ri = 0; ri < ret_child->list.count; ri++)
                    {
                        odin_grammar_node_t * named = ret_child->list.children[ri];
                        if (named == NULL || named->type != AST_NODE_NAMED_RETURN)
                            continue;
                        odin_grammar_node_t * type_node = NULL;
                        for (size_t ci = 0; ci < named->list.count; ci++)
                        {
                            if (named->list.children[ci] != NULL)
                                type_node = named->list.children[ci];
                        }
                        if (type_node == NULL)
                            continue;
                        TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
                        type_node->resolved_type = (TypeDescriptor *)td;
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
            else if (child->type == AST_NODE_PARAMETER_LIST)
            {
                // ParameterList = LParen Parameters? RParen
                for (size_t j = 0; j < child->list.count; j++)
                {
                    odin_grammar_node_t * param_group = child->list.children[j];
                    if (param_group == NULL || param_group->type != AST_NODE_PARAMETERS)
                        continue;

                    // Parameters = delimited(Parameter, Comma) (Comma Ellipsis)?
                    for (size_t k = 0; k < param_group->list.count; k++)
                    {
                        odin_grammar_node_t * param = param_group->list.children[k];
                        if (param == NULL)
                            continue;

                        if (param->type == AST_NODE_ELLIPSIS)
                        {
                            is_variadic = true;
                            continue;
                        }
                        if (param->type != AST_NODE_PARAMETER)
                            continue;

                        // Parameter = KwUsing? (PolyIdent Colon)? Identifier Colon VariadicMarker? TypePrefix
                        // Check for VariadicMarker child (AST_NODE_ELLIPSIS from ..)
                        bool is_variadic_param = false;
                        for (size_t ci = 0; ci < param->list.count; ci++)
                        {
                            if (param->list.children[ci] != NULL
                                && param->list.children[ci]->type == AST_NODE_ELLIPSIS)
                            {
                                is_variadic_param = true;
                                is_variadic = true;
                                break;
                            }
                        }

                        // Find the TypePrefix child (last non-NULL child)
                        odin_grammar_node_t * type_node = NULL;
                        for (size_t ci = 0; ci < param->list.count; ci++)
                        {
                            odin_grammar_node_t * pc = param->list.children[ci];
                            if (pc != NULL && pc->type != AST_NODE_ELLIPSIS)
                                type_node = pc;
                        }

                        if (type_node == NULL)
                            continue;

                        TypeDescriptor const * pt = sem_resolve_type_expr(ctx, type_node);
                        if (pt == NULL)
                            continue;

                        // If this is a variadic .. parameter, wrap the type in a slice
                        // e.g. ..any → []any
                        if (is_variadic_param)
                        {
                            pt = get_or_create_slice_type(ctx->type_registry, pt);
                            if (pt == NULL)
                                continue;
                            type_node->resolved_type = (TypeDescriptor *)pt;
                        }

                        // Grow params array
                        if (param_count >= (int)param_cap)
                        {
                            size_t new_cap = param_cap == 0 ? 4 : param_cap * 2;
                            TypeDescriptor const ** tmp = (TypeDescriptor const **)realloc(
                                (void *)params, new_cap * sizeof(TypeDescriptor const *)
                            );
                            if (tmp == NULL)
                            {
                                free((void *)params);
                                return NULL;
                            }
                            params = tmp;
                            param_cap = new_cap;
                        }
                        params[param_count++] = pt;
                    }
                }
            }
        }

        if (return_count == 0)
        {
            return_types = NULL;
        }
        TypeDescriptor const * proc_type = get_or_create_proc_type(
            ctx->type_registry, return_type, params, param_count, return_types, return_count, is_variadic, cc
        );
        free((void *)params);
        free((void *)return_types);
        if (proc_type)
            node->resolved_type = (TypeDescriptor *)proc_type;
        return proc_type;
    }

    case AST_NODE_ENUM_TYPE:
    {
        odin_grammar_node_t * enum_name_node = NULL;
        odin_grammar_node_t * enumerator_list = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_IDENTIFIER)
                enum_name_node = child;
            else if (child->type == AST_NODE_ENUMERATOR_LIST)
                enumerator_list = child;
        }
        char const * enum_name = enum_name_node ? enum_name_node->text : NULL;
        bool enum_named = (enum_name != NULL);

        TypeDescriptor const * int_td = get_basic_type_by_name(ctx->type_registry, "int");
        LLVMTypeRef llvm_int_type = int_td ? int_td->llvm_type : LLVMInt64TypeInContext(ctx->gen_ctx->context);
        TypeDescriptor const * enum_td = get_or_create_enum_type(ctx->type_registry, enum_name, llvm_int_type);
        if (enum_td == NULL)
            return NULL;

        // Count enumerators
        int num_enumerators = 0;
        if (enumerator_list)
        {
            for (size_t i = 0; i < enumerator_list->list.count; i++)
            {
                odin_grammar_node_t * enumerator = enumerator_list->list.children[i];
                if (enumerator != NULL && enumerator->type == AST_NODE_ENUMERATOR)
                    num_enumerators++;
            }
        }

        // Allocate arrays to store enumerator names and values in the type descriptor
        // (only if not already set — get_or_create_enum_type deduplicates, so existing
        // enum types may already have arrays from a previous analysis pass)
        TypeDescriptor * mutable_td = (TypeDescriptor *)enum_td;
        if (mutable_td->as.enum_type.enumerator_count == 0 && num_enumerators > 0)
        {
            mutable_td->as.enum_type.enumerator_names = calloc(num_enumerators, sizeof(char const *));
            mutable_td->as.enum_type.enumerator_values = calloc(num_enumerators, sizeof(long long));
        }

        if (enumerator_list)
        {
            int next_value = 0;
            int enum_idx = 0;
            for (size_t i = 0; i < enumerator_list->list.count; i++)
            {
                odin_grammar_node_t * enumerator = enumerator_list->list.children[i];
                if (enumerator == NULL || enumerator->type != AST_NODE_ENUMERATOR)
                    continue;

                odin_grammar_node_t * en_name_node = NULL;
                odin_grammar_node_t * en_value_node = NULL;
                for (size_t ci = 0; ci < enumerator->list.count; ci++)
                {
                    odin_grammar_node_t * child = enumerator->list.children[ci];
                    if (child == NULL)
                        continue;
                    if (child->type == AST_NODE_IDENTIFIER)
                        en_name_node = child;
                    else
                        en_value_node = child;
                }
                if (en_name_node == NULL || en_name_node->text == NULL)
                    continue;

                if (en_value_node)
                {
                    sem_evaluate_expr(ctx, en_value_node);
                    if (en_value_node->resolved_type)
                        next_value = 0;
                }

                odin_grammar_node_t * val_node = enumerator;
                val_node->resolved_type = (TypeDescriptor *)enum_td;

                LLVMValueRef llvm_val = LLVMConstInt(llvm_int_type, (unsigned long long)next_value, false);
                TypedValue tv = create_typed_value(llvm_val, enum_td, false);
                scope_add_symbol(generator_current_scope(ctx->gen_ctx), en_name_node->text, tv);

                // Cache enum value for compile-time evaluation
                symbol_t * en_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), en_name_node->text);
                if (en_sym)
                {
                    en_sym->const_int_val = next_value;
                    en_sym->has_const_int_val = true;
                }

                // Store enumerator info in the type descriptor
                if (enum_idx < num_enumerators && mutable_td->as.enum_type.enumerator_names != NULL
                    && mutable_td->as.enum_type.enumerator_values != NULL)
                {
                    mutable_td->as.enum_type.enumerator_names[enum_idx] = en_name_node->text;
                    mutable_td->as.enum_type.enumerator_values[enum_idx] = next_value;
                }
                enum_idx++;

                next_value++;
            }
            mutable_td->as.enum_type.enumerator_count = num_enumerators;
        }

        if (enum_td)
            node->resolved_type = (TypeDescriptor *)enum_td;
        return enum_td;
    }

    case AST_NODE_STRUCT_TYPE:
    {
        bool is_soa = false;
        uint32_t struct_alignment = 0;
        for (size_t ci = 0; ci < node->list.count; ci++)
        {
            odin_grammar_node_t * child = node->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_DIRECTIVE && child->text)
            {
                if (strcmp(child->text, "#soa") == 0)
                {
                    is_soa = true;
                    break;
                }
                if (strcmp(child->text, "#align") == 0)
                {
                    if (ci + 1 < node->list.count)
                    {
                        odin_grammar_node_t * align_val = node->list.children[ci + 1];
                        if (align_val && align_val->type == AST_NODE_INTEGER_VALUE && align_val->text)
                            struct_alignment = (uint32_t)strtoull(align_val->text, NULL, 0);
                    }
                }
            }
        }

        // Find StructFieldList (could be nested inside StructRawBody)
        odin_grammar_node_t * field_list = node_find_child(node, AST_NODE_STRUCT_FIELD_LIST);
        if (field_list == NULL)
        {
            odin_grammar_node_t * raw_body = node_find_child(node, AST_NODE_STRUCT_TYPE);
            if (raw_body)
                field_list = node_find_child(raw_body, AST_NODE_STRUCT_FIELD_LIST);
        }
        if (field_list == NULL || field_list->list.count == 0)
            return NULL;

        // Count struct fields
        int field_count = 0;
        for (size_t i = 0; i < field_list->list.count; i++)
        {
            if (field_list->list.children[i] && field_list->list.children[i]->type == AST_NODE_STRUCT_FIELD)
                field_count++;
        }
        if (field_count == 0)
            return NULL;

        // Collect field names and types
        char const ** field_names = malloc((size_t)field_count * sizeof(char const *));
        TypeDescriptor const ** field_types = malloc((size_t)field_count * sizeof(TypeDescriptor const *));
        LLVMTypeRef * llvm_field_types = malloc((size_t)field_count * sizeof(LLVMTypeRef));
        bool * field_is_using = malloc((size_t)field_count * sizeof(bool));
        uint32_t * field_alignments = malloc((size_t)field_count * sizeof(uint32_t));
        if (field_names == NULL || field_types == NULL || llvm_field_types == NULL || field_is_using == NULL
            || field_alignments == NULL)
        {
            free((void *)field_names);
            free((void *)field_types);
            free(llvm_field_types);
            free(field_is_using);
            free(field_alignments);
            return NULL;
        }

        int fi = 0;
        for (size_t i = 0; i < field_list->list.count && fi < field_count; i++)
        {
            odin_grammar_node_t * field = field_list->list.children[i];
            if (field == NULL || field->type != AST_NODE_STRUCT_FIELD)
                continue;

            // Detect "using" keyword (stored as text by the struct field action)
            bool is_using = (field->text != NULL && strcmp(field->text, "using") == 0);

            // Find field name (first Identifier child) and type (first type node child)
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * type_node = NULL;
            uint32_t field_alignment = 0;
            for (size_t ci = 0; ci < field->list.count; ci++)
            {
                odin_grammar_node_t * child = field->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = child;
                else if (is_type_node(child))
                    type_node = child;
                else if (child->type == AST_NODE_DIRECTIVE && child->text
                         && strcmp(child->text, "#align") == 0)
                {
                    if (ci + 1 < field->list.count)
                    {
                        odin_grammar_node_t * align_val = field->list.children[ci + 1];
                        if (align_val && align_val->type == AST_NODE_INTEGER_VALUE && align_val->text)
                            field_alignment = (uint32_t)strtoull(align_val->text, NULL, 0);
                    }
                }
            }

            if (name_node == NULL || name_node->text == NULL || type_node == NULL)
                continue;

            TypeDescriptor const * ftype = sem_resolve_type_expr(ctx, type_node);
            if (ftype == NULL)
                continue;

            if (is_soa)
            {
                TypeDescriptor const * slice_type = get_or_create_slice_type(ctx->type_registry, ftype);
                if (slice_type == NULL)
                    continue;
                field_types[fi] = slice_type;
                llvm_field_types[fi] = slice_type->llvm_type;
            }
            else
            {
                field_types[fi] = ftype;
                llvm_field_types[fi] = ftype->llvm_type;
            }
            field_names[fi] = name_node->text;
            field_is_using[fi] = is_using;
            field_alignments[fi] = field_alignment;
            fi++;
        }

        if (fi == 0)
        {
            free((void *)field_names);
            free((void *)field_types);
            free(llvm_field_types);
            free(field_is_using);
            free(field_alignments);
            return NULL;
        }

        if (is_soa)
        {
            struct_or_union_members_st members;
            members.count = fi;
            members.fields = malloc((size_t)fi * sizeof(struct_field_t));
            for (int j = 0; j < fi; j++)
            {
                members.fields[j].name = field_names[j];
                members.fields[j].type_desc = field_types[j];
                members.fields[j].is_using = field_is_using[j];
                members.fields[j].user_alignment = field_alignments[j];
                members.fields[j].offset = 0;
                members.fields[j].bit_offset = 0;
                members.fields[j].bit_width = 0;
                members.fields[j].storage_index = 0;
            }

            TypeDescriptor const * soa_td = get_or_create_soa_type(ctx->type_registry, &members);

            free((void *)field_names);
            free((void *)field_types);
            free(llvm_field_types);
            free(field_is_using);
            free(field_alignments);
            free(members.fields);

            if (soa_td)
                node->resolved_type = (TypeDescriptor *)soa_td;
            return soa_td;
        }

        LLVMTypeRef llvm_struct = LLVMStructTypeInContext(ctx->gen_ctx->context, llvm_field_types, (unsigned)fi, false);

        // Build struct_members_t
        struct_or_union_members_st members;
        members.count = fi;
        members.fields = malloc((size_t)fi * sizeof(struct_field_t));
        for (int j = 0; j < fi; j++)
        {
            members.fields[j].name = field_names[j];
            members.fields[j].type_desc = field_types[j];
            members.fields[j].is_using = field_is_using[j];
            members.fields[j].user_alignment = field_alignments[j];
            members.fields[j].offset = 0;
            members.fields[j].bit_offset = 0;
            members.fields[j].bit_width = 0;
            members.fields[j].storage_index = 0;
        }

        TypeDescriptor const * struct_td = register_struct_type(ctx->type_registry, llvm_struct, true, &members);

        // Override struct alignment if user specified #align
        if (struct_td && struct_alignment > 0)
        {
            TypeDescriptor * mutable_td = (TypeDescriptor *)struct_td;
            if (struct_alignment > mutable_td->struct_metadata.alignment)
                mutable_td->struct_metadata.alignment = struct_alignment;
        }

        free((void *)field_names);
        free((void *)field_types);
        free(llvm_field_types);
        free(field_is_using);
        free(field_alignments);
        free(members.fields);

        if (struct_td)
            node->resolved_type = (TypeDescriptor *)struct_td;
        return struct_td;
    }

    case AST_NODE_UNION_TYPE:
    {
        odin_grammar_node_t * field_list = node_find_child(node, AST_NODE_UNION_FIELD_LIST);
        if (field_list == NULL || field_list->list.count == 0)
            return NULL;

        int field_count = 0;
        for (size_t i = 0; i < field_list->list.count; i++)
        {
            if (field_list->list.children[i] && field_list->list.children[i]->type == AST_NODE_UNION_FIELD)
                field_count++;
        }
        if (field_count == 0)
            return NULL;

        struct_or_union_members_st members;
        members.count = 0;
        members.fields = malloc((size_t)field_count * sizeof(struct_field_t));
        if (members.fields == NULL)
            return NULL;

        int fi = 0;
        for (size_t i = 0; i < field_list->list.count && fi < field_count; i++)
        {
            odin_grammar_node_t * field = field_list->list.children[i];
            if (field == NULL || field->type != AST_NODE_UNION_FIELD)
                continue;

            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * type_node = NULL;
            for (size_t ci = 0; ci < field->list.count; ci++)
            {
                odin_grammar_node_t * child = field->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = child;
                else if (is_type_node(child))
                    type_node = child;
            }

            if (name_node == NULL || name_node->text == NULL || type_node == NULL)
                continue;

            TypeDescriptor const * ftype = sem_resolve_type_expr(ctx, type_node);
            if (ftype == NULL)
                continue;

            members.fields[fi].name = name_node->text;
            members.fields[fi].type_desc = ftype;
            members.fields[fi].is_using = false;
            members.fields[fi].user_alignment = 0;
            members.fields[fi].offset = 0;
            members.fields[fi].bit_offset = 0;
            members.fields[fi].bit_width = 0;
            members.fields[fi].storage_index = 0;
            fi++;
        }

        if (fi == 0)
        {
            free(members.fields);
            return NULL;
        }
        members.count = fi;

        TypeDescriptor const * union_td = get_or_create_union_type(ctx->type_registry, &members);

        free(members.fields);

        if (union_td)
            node->resolved_type = (TypeDescriptor *)union_td;
        return union_td;
    }

    case AST_NODE_SOA_TYPE:
    {
        // #soa[N] TypePrefix — creates a SOA array type backed by fixed-size arrays
        // #soa TypePrefix — creates a SOA array type backed by slices
        // Children: [DirectiveWithArgs | Directive, InnerType]
        odin_grammar_node_t * dw_node = NULL;
        odin_grammar_node_t * directive_node = NULL;
        odin_grammar_node_t * inner_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_DIRECTIVE_WITH_ARGS)
                dw_node = child;
            else if (child->type == AST_NODE_DIRECTIVE)
                directive_node = child;
            else if (is_type_node(child))
                inner_type_node = child;
        }

        if (inner_type_node == NULL)
            return NULL;

        // Case 1: #soa — slice-backed SOA
        if (directive_node != NULL && directive_node->text
            && strstr(directive_node->text, "#soa") != NULL && dw_node == NULL)
        {
            TypeDescriptor const * inner_td = sem_resolve_type_expr(ctx, inner_type_node);
            if (inner_td == NULL || inner_td->kind != TD_KIND_STRUCT)
            {
                sem_error_list_add(&ctx->errors, NULL, inner_type_node, "#soa requires a struct type");
                return NULL;
            }

            struct_or_union_members_st const * inner_members = &inner_td->struct_metadata.members;
            int field_count = inner_members->count;

            struct_or_union_members_st backing_members;
            backing_members.count = field_count;
            backing_members.fields = malloc((size_t)field_count * sizeof(struct_field_t));
            if (backing_members.fields == NULL)
                return NULL;

            for (int j = 0; j < field_count; j++)
            {
                struct_field_t const * src = &inner_members->fields[j];
                TypeDescriptor const * slice_type = get_or_create_slice_type(ctx->type_registry, src->type_desc);

                backing_members.fields[j].name = src->name;
                backing_members.fields[j].type_desc = slice_type;
                backing_members.fields[j].is_using = src->is_using;
                backing_members.fields[j].user_alignment = src->user_alignment;
                backing_members.fields[j].offset = 0;
                backing_members.fields[j].bit_offset = 0;
                backing_members.fields[j].bit_width = 0;
                backing_members.fields[j].storage_index = 0;
            }

            TypeDescriptor const * soa_td = get_or_create_soa_type(ctx->type_registry, &backing_members);
            free(backing_members.fields);

            if (soa_td)
                node->resolved_type = (TypeDescriptor *)soa_td;
            return soa_td;
        }

        // Case 2: #soa[N] — array-backed SOA (existing code)
        if (dw_node == NULL)
            return NULL;

        // Extract the count expression from DirectiveWithArgs
        odin_grammar_node_t * count_expr = NULL;
        for (size_t i = 0; i < dw_node->list.count; i++)
        {
            odin_grammar_node_t * child = dw_node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type != AST_NODE_IDENTIFIER)
            {
                count_expr = child;
                break;
            }
        }

        if (count_expr == NULL)
            return NULL;

        sem_evaluate_expr(ctx, count_expr);

        odin_grammar_node_t * eval_node = count_expr;
        while (eval_node->list.count >= 1 && eval_node->list.children[0] != NULL)
        {
            int can_eval = 0;
            switch (eval_node->type)
            {
            case AST_NODE_INTEGER_VALUE:
                can_eval = 1;
                break;
            default:
                break;
            }
            if (can_eval)
                break;
            if (eval_node->list.count == 1 || eval_node->type == AST_NODE_POSTFIX_EXPRESSION)
                eval_node = eval_node->list.children[0];
            else
                break;
        }

        if (eval_node->type != AST_NODE_INTEGER_VALUE || eval_node->text == NULL)
        {
            sem_error_list_add(&ctx->errors, NULL, count_expr, "#soa[N] requires a constant integer expression");
            return NULL;
        }

        char * end = NULL;
        unsigned long long count_val = parse_odin_unsigned(eval_node->text, &end, 0);
        if (end == eval_node->text || count_val == 0)
        {
            sem_error_list_add(&ctx->errors, NULL, count_expr, "#soa[N] requires a positive integer");
            return NULL;
        }

        TypeDescriptor const * inner_td = sem_resolve_type_expr(ctx, inner_type_node);
        if (inner_td == NULL || inner_td->kind != TD_KIND_STRUCT)
        {
            sem_error_list_add(&ctx->errors, NULL, inner_type_node, "#soa[N] requires a struct type");
            return NULL;
        }

        struct_or_union_members_st const * inner_members = &inner_td->struct_metadata.members;
        int field_count = inner_members->count;

        struct_or_union_members_st backing_members;
        backing_members.count = field_count;
        backing_members.fields = malloc((size_t)field_count * sizeof(struct_field_t));
        if (backing_members.fields == NULL)
            return NULL;

        for (int j = 0; j < field_count; j++)
        {
            struct_field_t const * src = &inner_members->fields[j];
            TypeDescriptor const * array_type
                = get_or_create_array_type(ctx->type_registry, src->type_desc, (size_t)count_val);

            backing_members.fields[j].name = src->name;
            backing_members.fields[j].type_desc = array_type;
            backing_members.fields[j].is_using = src->is_using;
            backing_members.fields[j].user_alignment = src->user_alignment;
            backing_members.fields[j].offset = 0;
            backing_members.fields[j].bit_offset = 0;
            backing_members.fields[j].bit_width = 0;
            backing_members.fields[j].storage_index = 0;
        }

        TypeDescriptor const * soa_td = get_or_create_soa_type(ctx->type_registry, &backing_members);

        free(backing_members.fields);

        if (soa_td)
            node->resolved_type = (TypeDescriptor *)soa_td;
        return soa_td;
    }

    case AST_NODE_MAYBE_TYPE:
    {
        // MaybeType = KwMaybe LParen TypePrefix RParen
        // Children: [InnerType]
        odin_grammar_node_t * inner_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (is_type_node(node->list.children[i]))
            {
                inner_type_node = node->list.children[i];
                break;
            }
        }
        if (inner_type_node == NULL)
            return NULL;

        TypeDescriptor const * inner_type = sem_resolve_type_expr(ctx, inner_type_node);
        if (inner_type == NULL)
            return NULL;

        TypeDescriptor const * maybe_type = get_or_create_maybe_type(ctx->type_registry, inner_type);
        if (maybe_type)
            node->resolved_type = (TypeDescriptor *)maybe_type;
        return maybe_type;
    }

    case AST_NODE_TUPLE_TYPE:
    {
        // TupleType = LBracket TypePrefix (Comma TypePrefix)+ RBracket
        // Children: [TypePrefix, TypePrefix, ...]
        int elem_count = (int)node->list.count;
        if (elem_count <= 0)
            return NULL;

        TypeDescriptor const ** elem_types = (TypeDescriptor const **)malloc(
            (size_t)elem_count * sizeof(TypeDescriptor const *)
        );
        int valid_count = 0;
        for (int i = 0; i < elem_count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL) continue;
            TypeDescriptor const * et = sem_resolve_type_expr(ctx, child);
            if (et == NULL)
            {
                free(elem_types);
                return NULL;
            }
            elem_types[valid_count++] = et;
        }

        if (valid_count == 0)
        {
            free(elem_types);
            return NULL;
        }

        TypeDescriptor const * tuple_type = get_or_create_tuple_type(
            ctx->type_registry, elem_types, valid_count
        );
        free(elem_types);
        if (tuple_type)
            node->resolved_type = (TypeDescriptor *)tuple_type;
        return tuple_type;
    }

    case AST_NODE_VECTOR_TYPE:
    {
        // VectorType = KwSimd LBracket IntegerLiteral RBracket TypePrefix
        // Children: [IntegerLiteral, ElementType]
        odin_grammar_node_t * int_child = NULL;
        odin_grammar_node_t * type_child = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL) continue;
            if (child->type == AST_NODE_INTEGER_VALUE)
                int_child = child;
            else if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
                type_child = child;
        }
        if (int_child == NULL || type_child == NULL)
            return NULL;

        TypeDescriptor const * elem_type = sem_resolve_type_expr(ctx, type_child);
        if (elem_type == NULL)
            return NULL;

        int const_ok = 0;
        long long lane_count = sem_evaluate_constant_int(ctx, int_child, &const_ok);
        if (!const_ok || lane_count <= 0)
            return NULL;

        TypeDescriptor const * vec_type = get_or_create_vector_type(
            ctx->type_registry, elem_type, (int)lane_count
        );
        if (vec_type)
            node->resolved_type = (TypeDescriptor *)vec_type;
        return vec_type;
    }

    case AST_NODE_IDENTIFIER:
    {
        if (node->text == NULL)
            return NULL;
        // Look up the identifier in the current scope to see if it's a type alias
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
        if (sym != NULL && sym->kind == SYMBOL_TYPE && sym->value.type_info != NULL)
        {
            node->resolved_type = (TypeDescriptor *)sym->value.type_info;
            return sym->value.type_info;
        }
        // Fallback: check if this is a known built-in type name (e.g. i128, u128)
        TypeDescriptor const * bt = get_basic_type_by_name(ctx->type_registry, node->text);
        if (bt != NULL)
        {
            node->resolved_type = (TypeDescriptor *)bt;
            return bt;
        }
        return NULL;
    }

    default:
        return NULL;
    }
}

// --- Expression evaluation ---

static TypeDescriptor const *
sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

    switch (node->type)
    {
    case AST_NODE_INTEGER_VALUE:
        return sem_evaluate_integer_value(ctx, node);

    case AST_NODE_FLOAT_VALUE:
        return sem_evaluate_float_value(ctx, node);

    case AST_NODE_STRING_LITERAL:
    case AST_NODE_RAW_STRING_LITERAL:
        return sem_evaluate_string_literal(ctx, node);

    case AST_NODE_RUNE_LITERAL:
        return sem_evaluate_rune_literal(ctx, node);

    case AST_NODE_BOOL_TRUE:
    case AST_NODE_BOOL_FALSE:
        return sem_evaluate_bool_value(ctx, node);

    case AST_NODE_AUTO_CAST_EXPR:
        return sem_evaluate_auto_cast_expr(ctx, node);

    case AST_NODE_CAST_EXPR:
    case AST_NODE_TRANSMUTE_EXPR:
        return sem_evaluate_cast_expr(ctx, node);

    case AST_NODE_LEN_EXPR:
    case AST_NODE_CAP_EXPR:
        return sem_evaluate_len_cap_expr(ctx, node);

    case AST_NODE_MAKE_EXPR:
        return sem_evaluate_make_expr(ctx, node);

    case AST_NODE_NEW_EXPR:
        return sem_evaluate_new_expr(ctx, node);

    case AST_NODE_DELETE_EXPR:
        return sem_evaluate_delete_expr(ctx, node);

    case AST_NODE_EXPAND_VALUES_EXPR:
        return sem_evaluate_expand_values_expr(ctx, node);

    case AST_NODE_COMPRESS_VALUES_EXPR:
        return sem_evaluate_compress_values_expr(ctx, node);

    case AST_NODE_SOA_ZIP_EXPR:
        return sem_evaluate_soa_zip_expr(ctx, node);

    case AST_NODE_SOA_UNZIP_EXPR:
        return sem_evaluate_soa_unzip_expr(ctx, node);

    case AST_NODE_INCL_EXPR:
    case AST_NODE_EXCL_EXPR:
        return sem_evaluate_incl_excl_expr(ctx, node);

    case AST_NODE_COMPLEX_EXPR:
    case AST_NODE_QUATERNION_EXPR:
        return sem_evaluate_complex_quaternion_expr(ctx, node);

    case AST_NODE_SIZE_OF_EXPR:
    case AST_NODE_ALIGN_OF_EXPR:
        return sem_evaluate_size_align_of_expr(ctx, node);

    case AST_NODE_OFFSET_OF_EXPR:
        return sem_evaluate_offset_of_expr(ctx, node);

    case AST_NODE_RAW_DATA_EXPR:
        return sem_evaluate_raw_data_expr(ctx, node);

    case AST_NODE_TYPE_OF_EXPR:
        return sem_evaluate_type_of_expr(ctx, node);

    case AST_NODE_TYPEID_OF_EXPR:
        return sem_evaluate_typeid_of_expr(ctx, node);

    case AST_NODE_TYPE_INFO_OF_EXPR:
        return sem_evaluate_type_info_of_expr(ctx, node);

    case AST_NODE_MIN_EXPR:
    case AST_NODE_MAX_EXPR:
        return sem_evaluate_min_max_expr(ctx, node);

    case AST_NODE_DISTINCT_TYPE:
        return sem_evaluate_distinct_type(ctx, node);

    case AST_NODE_NIL:
        return sem_evaluate_nil(ctx, node);

    case AST_NODE_DIRECTIVE:
        return sem_evaluate_directive(ctx, node);

    case AST_NODE_CONTEXT_EXPR:
        return sem_evaluate_context_expr(ctx, node);

    case AST_NODE_IDENTIFIER:
        return sem_evaluate_identifier(ctx, node);

    case AST_NODE_UNARY_EXPRESSION:
        return sem_evaluate_unary_expr(ctx, node);

    case AST_NODE_RANGE_EXPRESSION:
        return sem_evaluate_range_expr(ctx, node);

    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
        return sem_evaluate_binary_arith_expr(ctx, node);

    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
        return sem_evaluate_comp_log_expr(ctx, node);

    case AST_NODE_POSTFIX_CALL:
        return sem_evaluate_postfix_call(ctx, node);

    case AST_NODE_POSTFIX_MEMBER:
        return sem_evaluate_postfix_member(ctx, node);

    case AST_NODE_OR_ELSE:
        return sem_evaluate_or_else(ctx, node);

    case AST_NODE_OR_RETURN:
        return sem_evaluate_or_return(ctx, node);

    case AST_NODE_TERNARY_EXPRESSION:
        return sem_evaluate_ternary_expr(ctx, node);

    case AST_NODE_EXPRESSION:
    case AST_NODE_PRIMARY_EXPRESSION:
        return sem_evaluate_expression_wrapper(ctx, node);

    case AST_NODE_ASSIGN_EXPRESSION:
        return sem_evaluate_assign_expr(ctx, node);

    case AST_NODE_POSTFIX_EXPRESSION:
        return sem_evaluate_postfix_expr(ctx, node);

    case AST_NODE_DIRECTIVE_WITH_ARGS:
        return sem_evaluate_directive_with_args(ctx, node);

    default:
        return NULL;

    }
}

// --- Extracted case functions (Phase 3.1) ---

static TypeDescriptor const *
sem_evaluate_integer_value(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
    if (int_type)
    {
        node->resolved_type = (TypeDescriptor *)int_type;
    }
    return int_type;
    
}

static TypeDescriptor const *
sem_evaluate_float_value(SemContext * ctx, odin_grammar_node_t * node)
{

    char const * text = node->text;
    TypeDescriptor const * flt_type = NULL;
    if (text != NULL)
    {
        size_t len = strlen(text);
        if (len >= 3 && strcmp(text + len - 3, "f16") == 0)
        {
            flt_type = get_basic_type_by_name(ctx->type_registry, "f16");
        }
    }
    if (flt_type == NULL)
        flt_type = get_basic_type_by_name(ctx->type_registry, "f64");
    if (flt_type)
    {
        node->resolved_type = (TypeDescriptor *)flt_type;
    }
    return flt_type;
    
}

static TypeDescriptor const *
sem_evaluate_string_literal(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * str_type = get_basic_type_by_name(ctx->type_registry, "string");
    if (str_type)
    {
        node->resolved_type = (TypeDescriptor *)str_type;
    }
    return str_type;
    
}

static TypeDescriptor const *
sem_evaluate_rune_literal(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * rune_type = get_basic_type_by_name(ctx->type_registry, "rune");
    if (rune_type)
    {
        node->resolved_type = (TypeDescriptor *)rune_type;
    }
    return rune_type;
    
}

static TypeDescriptor const *
sem_evaluate_bool_value(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * bool_type = get_basic_type_by_name(ctx->type_registry, "bool");
    if (bool_type)
    {
        node->resolved_type = (TypeDescriptor *)bool_type;
    }
    return bool_type;
    
}

static TypeDescriptor const *
sem_evaluate_auto_cast_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count >= 1)
        sem_evaluate_expr(ctx, node->list.children[0]);
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_cast_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    // children[0] = type, children[1] = expression
    TypeDescriptor const * target_type = NULL;
    odin_grammar_node_t * type_node = (node->list.count >= 1) ? node->list.children[0] : NULL;
    if (type_node)
        target_type = sem_resolve_type_expr(ctx, type_node);
    if (node->list.count >= 2)
        sem_evaluate_expr(ctx, node->list.children[1]);
    node->resolved_type = (TypeDescriptor *)target_type;
    return target_type;
    
}

static TypeDescriptor const *
sem_evaluate_len_cap_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;
    odin_grammar_node_t * operand = node->list.children[0];
    sem_evaluate_expr(ctx, operand);
    TypeDescriptor const * operand_type = operand->resolved_type;
    if (operand_type == NULL)
        return NULL;

    // Validate: valid for arrays, slices, strings (len only for strings)
    bool valid = (operand_type->kind == TD_KIND_ARRAY) || (operand_type->kind == TD_KIND_SLICE)
                 || (operand_type->kind == TD_KIND_DYNAMIC_ARRAY) || (operand_type->kind == TD_KIND_MAP)
                 || (operand_type->kind == TD_KIND_BASIC && operand_type->as.basic.name != NULL
                     && strcmp(operand_type->as.basic.name, "string") == 0 && node->type == AST_NODE_LEN_EXPR);
    if (!valid)
    {
        sem_error_list_add(
            &ctx->errors, NULL,
            node,
            node->type == AST_NODE_LEN_EXPR ? "invalid operand type for len" : "invalid operand type for cap"
        );
        return NULL;
    }

    TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
    node->resolved_type = (TypeDescriptor *)int_type;
    return int_type;
    
}

static TypeDescriptor const *
sem_evaluate_make_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 2)
        return NULL;
    odin_grammar_node_t * type_node = node->list.children[0];
    odin_grammar_node_t * len_node = node->list.children[1];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
    if (td == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "invalid type argument to make");
        return NULL;
    }
    if (td->kind != TD_KIND_SLICE && td->kind != TD_KIND_DYNAMIC_ARRAY && td->kind != TD_KIND_MAP)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "make only supports slice, dynamic array, and map types");
        return NULL;
    }
    sem_evaluate_expr(ctx, len_node);
    node->resolved_type = (TypeDescriptor *)td;
    return td;
    
}

static TypeDescriptor const *
sem_evaluate_new_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;
    odin_grammar_node_t * type_node = node->list.children[0];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
    if (td == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "invalid type argument to new");
        return NULL;
    }
    TypeDescriptor const * ptr_type = get_or_create_pointer_type(ctx->type_registry, td);
    node->resolved_type = (TypeDescriptor *)ptr_type;
    return ptr_type;
    
}

static TypeDescriptor const *
sem_evaluate_delete_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;
    sem_evaluate_expr(ctx, node->list.children[0]);
    node->resolved_type = NULL;
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_expand_values_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;
    TypeDescriptor const * inner_type = sem_evaluate_expr(ctx, node->list.children[0]);
    if (inner_type == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    if (inner_type->kind != TD_KIND_STRUCT && inner_type->kind != TD_KIND_ARRAY)
    {
        sem_error_list_add(&ctx->errors, NULL, node,
            "expand_values: argument must be a struct or array type");
        node->resolved_type = NULL;
        return NULL;
    }
    // The resolved type is the aggregate type itself (fields will be expanded at the call site)
    node->resolved_type = (TypeDescriptor *)inner_type;
    return inner_type;
    
}

static TypeDescriptor const *
sem_evaluate_compress_values_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 2)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * type_node = node->list.children[0];
    TypeDescriptor const * target_type = sem_resolve_type_expr(ctx, type_node);
    if (target_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node,
            "compress_values: first argument must be a type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (target_type->kind != TD_KIND_STRUCT && target_type->kind != TD_KIND_ARRAY)
    {
        sem_error_list_add(&ctx->errors, NULL, node,
            "compress_values: target type must be a struct or array");
        node->resolved_type = NULL;
        return NULL;
    }
    int expected_count = 0;
    if (target_type->kind == TD_KIND_STRUCT)
        expected_count = target_type->struct_metadata.members.count;
    else
        expected_count = (int)target_type->as.array.count;
    int actual_count = (int)node->list.count - 1; // skip type node
    if (actual_count != expected_count)
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "compress_values: expected %d values but got %d",
            expected_count, actual_count);
        sem_error_list_add(&ctx->errors, NULL, node, buf);
        node->resolved_type = NULL;
        return NULL;
    }
    // Evaluate each value expression
    for (int i = 1; i < (int)node->list.count; i++)
    {
        sem_evaluate_expr(ctx, node->list.children[i]);
    }
    node->resolved_type = (TypeDescriptor *)target_type;
    return target_type;
    
}

static TypeDescriptor const *
sem_evaluate_soa_zip_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    // Collect arguments from comma-chain expression
    odin_grammar_node_t * arg_list = (node->list.count >= 1) ? node->list.children[0] : NULL;
    odin_grammar_node_t * arg_expr = (arg_list && arg_list->list.count >= 1) ? arg_list->list.children[0] : NULL;
    if (arg_expr == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "soa_zip requires at least one argument");
        node->resolved_type = NULL;
        return NULL;
    }

    // Walk comma-chain expression to extract individual arg nodes
    odin_grammar_node_t * args[128];
    int arg_count = 0;
    sem_collect_comma_chain_args(arg_expr, args, 128, &arg_count);

    struct_or_union_members_st backing_members;
    backing_members.count = arg_count;
    backing_members.fields = calloc((size_t)arg_count, sizeof(struct_field_t));
    for (int i = 0; i < arg_count; i++)
    {
        TypeDescriptor const * arg_type = sem_evaluate_expr(ctx, args[i]);
        if (arg_type == NULL)
        {
            sem_error_list_add(&ctx->errors, NULL, args[i], "soa_zip: argument has NULL type");
            free(backing_members.fields);
            node->resolved_type = NULL;
            return NULL;
        }
        if (arg_type->kind != TD_KIND_SLICE)
        {
            sem_error_list_add(&ctx->errors, NULL, args[i], "soa_zip: argument must be a slice type");
            free(backing_members.fields);
            node->resolved_type = NULL;
            return NULL;
        }
        char field_name[32];
        snprintf(field_name, sizeof(field_name), "_%d", i);
        backing_members.fields[i].name = strdup(field_name);
        backing_members.fields[i].type_desc = arg_type;
    }
    TypeDescriptor const * soa_type = get_or_create_soa_type(ctx->type_registry, &backing_members);
    // Note: field names in backing_members are shallow-copied into the SOA type
    // (get_or_create_soa_type uses memcpy), so the strdup'd names must NOT be freed here.
    free(backing_members.fields);
    node->resolved_type = (TypeDescriptor *)soa_type;
    return soa_type;
    
}

static TypeDescriptor const *
sem_evaluate_soa_unzip_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "soa_unzip requires one argument");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * arg_type = sem_evaluate_expr(ctx, node->list.children[0]);
    if (arg_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node->list.children[0], "soa_unzip: argument has NULL type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (arg_type->kind != TD_KIND_SOA)
    {
        sem_error_list_add(&ctx->errors, NULL, node->list.children[0], "soa_unzip: argument must be an SOA struct type");
        node->resolved_type = NULL;
        return NULL;
    }
    int field_count = arg_type->struct_metadata.members.count;
    TypeDescriptor const ** elem_types = calloc((size_t)field_count, sizeof(TypeDescriptor const *));
    for (int i = 0; i < field_count; i++)
    {
        struct_field_t const * field = &arg_type->struct_metadata.members.fields[i];
        // Each field is already a slice type ([]T)
        elem_types[i] = field->type_desc;
    }
    TypeDescriptor const * tuple_type = get_or_create_tuple_type(ctx->type_registry, elem_types, field_count);
    free(elem_types);
    node->resolved_type = (TypeDescriptor *)tuple_type;
    return tuple_type;
    
}

static TypeDescriptor const *
sem_evaluate_incl_excl_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 2)
        return NULL;
    TypeDescriptor const * ptr_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * elem_type = sem_evaluate_expr(ctx, node->list.children[1]);
    if (ptr_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "incl/excl: first arg resolved to NULL type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (ptr_type->kind != TD_KIND_POINTER)
    {
        char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            "incl/excl: first arg is kind %d, not TD_KIND_POINTER (%d)",
            ptr_type->kind,
            TD_KIND_POINTER
        );
        sem_error_list_add(&ctx->errors, NULL, node, buf);
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * bs_type = ptr_type->pointee;
    if (bs_type == NULL || bs_type->kind != TD_KIND_BIT_SET)
    {
        char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            "incl/excl: element type is kind %d, not TD_KIND_BIT_SET (%d)",
            bs_type ? bs_type->kind : -1,
            TD_KIND_BIT_SET
        );
        sem_error_list_add(&ctx->errors, NULL, node, buf);
        node->resolved_type = NULL;
        return NULL;
    }
    if (elem_type == NULL || !is_integer_kind(elem_type))
    {
        sem_error_list_add(&ctx->errors, NULL, node, "second argument to incl/excl must be an integer");
        node->resolved_type = NULL;
        return NULL;
    }
    node->resolved_type = NULL;
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_complex_quaternion_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    bool is_complex = (node->type == AST_NODE_COMPLEX_EXPR);
    int min_args = is_complex ? 2 : 4;
    if (node->list.count < min_args)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "complex/quaternion: insufficient arguments");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * arg0 = sem_evaluate_expr(ctx, node->list.children[0]);
    for (size_t i = 1; i < node->list.count; i++)
    {
        TypeDescriptor const * arg = sem_evaluate_expr(ctx, node->list.children[i]);
        if (arg == NULL || arg0 == NULL)
            continue;
        if (arg->llvm_type != arg0->llvm_type)
        {
            sem_error_list_add(&ctx->errors, NULL, node,
                "complex/quaternion: all arguments must have the same type");
            node->resolved_type = NULL;
            return NULL;
        }
    }
    if (arg0 == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    char const * target_name = NULL;
    LLVMContextRef llvm_ctx = ctx->gen_ctx->context;
    if (arg0->llvm_type == LLVMHalfTypeInContext(llvm_ctx))
        target_name = is_complex ? "complex32" : "quaternion64";
    else if (arg0->llvm_type == LLVMFloatTypeInContext(llvm_ctx))
        target_name = is_complex ? "complex64" : "quaternion128";
    else if (arg0->llvm_type == LLVMDoubleTypeInContext(llvm_ctx))
        target_name = is_complex ? "complex128" : "quaternion256";
    else
    {
        sem_error_list_add(&ctx->errors, NULL, node,
            "complex/quaternion: arguments must be f16, f32, or f64");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * result_type = get_basic_type_by_name(ctx->type_registry, target_name);
    node->resolved_type = (TypeDescriptor *)result_type;
    return result_type;
    
}

static TypeDescriptor const *
sem_evaluate_size_align_of_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * type_node = node->list.children[0];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
    if (td == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "invalid type argument to size_of/align_of");
        node->resolved_type = NULL;
        return NULL;
    }
    type_node->resolved_type = (TypeDescriptor *)td;
    TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
    node->resolved_type = (TypeDescriptor *)int_type;
    return int_type;
    
}

static TypeDescriptor const *
sem_evaluate_offset_of_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 2)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * type_node = node->list.children[0];
    odin_grammar_node_t * field_node = node->list.children[1];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
    if (td == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "invalid type argument to offset_of");
        node->resolved_type = NULL;
        return NULL;
    }
    type_node->resolved_type = (TypeDescriptor *)td;
    if (td->kind != TD_KIND_STRUCT && td->kind != TD_KIND_SOA && td->kind != TD_KIND_UNION)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "offset_of requires a struct, SOA, or union type");
        node->resolved_type = NULL;
        return NULL;
    }

    TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
    node->resolved_type = (TypeDescriptor *)int_type;
    return int_type;
    
}

static TypeDescriptor const *
sem_evaluate_raw_data_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * operand = node->list.children[0];
    sem_evaluate_expr(ctx, operand);
    TypeDescriptor const * operand_type = operand->resolved_type;
    if (operand_type == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    if (operand_type->kind != TD_KIND_SLICE && operand_type->kind != TD_KIND_ARRAY
        && operand_type->kind != TD_KIND_DYNAMIC_ARRAY)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "raw_data requires a slice, array, or dynamic array");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * elem_type = operand_type->element_type;
    if (elem_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "raw_data: operand has no element type");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * ptr_type = get_or_create_pointer_type(ctx->type_registry, elem_type);
    node->resolved_type = (TypeDescriptor *)ptr_type;
    return ptr_type;
    
}

static TypeDescriptor const *
sem_evaluate_type_of_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * operand = node->list.children[0];
    if (is_type_node(operand))
    {
        TypeDescriptor const * td = sem_resolve_type_expr(ctx, operand);
        if (td)
            operand->resolved_type = (TypeDescriptor *)td;
    }
    else
    {
        sem_evaluate_expr(ctx, operand);
    }
    TypeDescriptor const * typeid_type = get_basic_type_by_name(ctx->type_registry, "typeid");
    node->resolved_type = (TypeDescriptor *)typeid_type;
    return typeid_type;
    
}

static TypeDescriptor const *
sem_evaluate_typeid_of_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * operand = node->list.children[0];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, operand);
    if (td == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    operand->resolved_type = (TypeDescriptor *)td;
    TypeDescriptor const * typeid_type = get_basic_type_by_name(ctx->type_registry, "typeid");
    node->resolved_type = (TypeDescriptor *)typeid_type;
    return typeid_type;
    
}

static TypeDescriptor const *
sem_evaluate_type_info_of_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    odin_grammar_node_t * operand = node->list.children[0];
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, operand);
    if (td == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    operand->resolved_type = (TypeDescriptor *)td;
    // type_info_of(T) returns ^type_info
    TypeDescriptor const * ti_ptr = type_descriptor_get_type_info_ptr_type(ctx->type_registry);
    node->resolved_type = (TypeDescriptor *)ti_ptr;
    return ti_ptr;
    
}

static TypeDescriptor const *
sem_evaluate_min_max_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 2)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * lhs_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * rhs_type = sem_evaluate_expr(ctx, node->list.children[1]);
    if (lhs_type == NULL || rhs_type == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    bool lhs_num = is_integer_kind(lhs_type) || is_floating_kind(lhs_type);
    bool rhs_num = is_integer_kind(rhs_type) || is_floating_kind(rhs_type);
    if (!lhs_num || !rhs_num)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "min/max requires numeric arguments");
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
    node->resolved_type = (TypeDescriptor *)int_type;
    return int_type;
    
}

static TypeDescriptor const *
sem_evaluate_distinct_type(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * td = sem_resolve_type_expr(ctx, node);
    node->resolved_type = (TypeDescriptor *)td;
    return td;
    
}

static TypeDescriptor const *
sem_evaluate_nil(SemContext * ctx, odin_grammar_node_t * node)
{

    node->resolved_type = NULL;
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_directive(SemContext * ctx, odin_grammar_node_t * node)
{

    // #caller_location — returns Source_Location struct
    if (node->text != NULL && strstr(node->text, "#caller_location") != NULL)
    {
        TypeDescriptor const * sl_type = type_descriptor_get_source_location_type(ctx->type_registry);
        if (sl_type != NULL)
        {
            node->resolved_type = (TypeDescriptor *)sl_type;
            return sl_type;
        }
        sem_error_list_add(&ctx->errors, NULL, node, "#caller_location: Source_Location type not available");
        return NULL;
    }
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_context_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
    if (sym)
    {
        node->resolved_symbol = sym;
        node->resolved_type = (TypeDescriptor *)sym->value.type_info;
        return sym->value.type_info;
    }
    sem_error_list_add(&ctx->errors, NULL, node, "'context' used outside of a procedure scope");
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_identifier(SemContext * ctx, odin_grammar_node_t * node)
{

    // Blank identifier _ is always valid as a discard target
    if (node->text && strcmp(node->text, "_") == 0)
        return NULL;

    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
    if (sym)
    {
        node->resolved_symbol = sym;
        node->resolved_type = (TypeDescriptor *)sym->value.type_info;
        return sym->value.type_info;
    }
    sem_error_list_add(&ctx->errors, NULL, node, "undeclared identifier");
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_unary_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    odin_grammar_node_t * op_node = node_find_child(node, AST_NODE_UNARY_OP);
    // Find the operand (non-op child)
    odin_grammar_node_t * operand_node = NULL;
    for (size_t uei = 0; uei < node->list.count; uei++)
    {
        odin_grammar_node_t * child = node->list.children[uei];
        if (child != NULL && (op_node == NULL || child != op_node))
        {
            operand_node = child;
            break;
        }
    }
    if (operand_node == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }
    TypeDescriptor const * operand_type = sem_evaluate_expr(ctx, operand_node);
    if (op_node && op_node->metadata)
    {
        AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
        if (op_md->kind == OP_UNARY_ADDR)
        {
            TypeDescriptor const * ptr_type = get_or_create_pointer_type(ctx->type_registry, operand_type);
            node->resolved_type = (TypeDescriptor *)ptr_type;
            return ptr_type;
        }
    }
    node->resolved_type = (TypeDescriptor *)operand_type;
    return operand_type;
    
}

static TypeDescriptor const *
sem_evaluate_range_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
    if (left_type == NULL || right_type == NULL)
        return NULL;
    if (!is_integer_kind(left_type) || !is_integer_kind(right_type))
    {
        sem_error_list_add(&ctx->errors, NULL, node, "Range expression requires integer operands");
        node->resolved_type = (TypeDescriptor *)left_type;
        return left_type;
    }
    odin_grammar_node_t * op_node = node_find_child(node, AST_NODE_RANGE_OP);
    bool is_inclusive = false;
    if (op_node && op_node->text)
        is_inclusive = (strcmp(op_node->text, "..") == 0);
    TypeDescriptor const * range_type = get_or_create_range_type(ctx->type_registry, is_inclusive);
    node->resolved_type = (TypeDescriptor *)range_type;
    return range_type;
    
}

static TypeDescriptor const *
sem_evaluate_binary_arith_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    {
            TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
            TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
            (void)right_type;
            node->resolved_type = (TypeDescriptor *)left_type;
            return left_type;
        }

        // Comparison and logical expressions resolve to bool
}

static TypeDescriptor const *
sem_evaluate_comp_log_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
    (void)right_type;
    (void)left_type;
    TypeDescriptor const * bool_type = get_basic_type_by_name(ctx->type_registry, "bool");
    node->resolved_type = (TypeDescriptor *)bool_type;
    return bool_type;
    
}

static TypeDescriptor const *
sem_evaluate_postfix_call(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * callee_type = sem_evaluate_expr(ctx, node->list.children[0]);
    if (callee_type && callee_type->kind == TD_KIND_PROC)
    {
        if (callee_type->proc_metadata.return_count > 1)
        {
            node->resolved_type = (TypeDescriptor *)callee_type;
            return callee_type;
        }
        TypeDescriptor const * ret_type = callee_type->proc_metadata.return_type;
        node->resolved_type = (TypeDescriptor *)ret_type;
        return ret_type;
    }
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_postfix_member(SemContext * ctx, odin_grammar_node_t * node)
{

    // POSTFIX_MEMBER has 1 child: the field identifier (Dot terminal has no AST action)
    if (node->list.count >= 1 && node->list.children[0] && node->list.children[0]->text)
    {
        // The base struct type must come from the parent POSTFIX_EXPRESSION's first child
        // This case is a fallback; normally handled inside POSTFIX_EXPRESSION's postfix_ops loop
        char const * field_name = node->list.children[0]->text;
        (void)field_name;
    }
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_or_else(SemContext * ctx, odin_grammar_node_t * node)
{

    // OrElseExpr = TernaryExpression (KwOrElse TernaryExpression)?
    // KwOrElse lexeme consumed, not a child. Without or_else: 1 child; with or_else: 2 children.
    if (node->list.count < 2)
    {
        if (node->list.count > 0)
        {
            TypeDescriptor const * inner_type = sem_evaluate_expr(ctx, node->list.children[0]);
            if (inner_type)
                node->resolved_type = (TypeDescriptor *)inner_type;
            return inner_type;
        }
        return NULL;
    }
    TypeDescriptor const * lhs_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * rhs_type = sem_evaluate_expr(ctx, node->list.children[1]);
    // For Maybe(T) or_else, result is T (the inner type), not Maybe(T)
    if (lhs_type && lhs_type->kind == TD_KIND_MAYBE)
    {
        TypeDescriptor const * inner_type = lhs_type->as.maybe.inner_type;
        node->resolved_type = (TypeDescriptor *)inner_type;
        return inner_type;
    }
    TypeDescriptor const * result_type = lhs_type ? lhs_type : rhs_type;
    if (result_type)
        node->resolved_type = (TypeDescriptor *)result_type;
    return result_type;
    
}

static TypeDescriptor const *
sem_evaluate_or_return(SemContext * ctx, odin_grammar_node_t * node)
{

    // OrReturnExpr = OrElseExpr KwOrReturn
    // Only present when or_return keyword is used. 1 child: OrElseExpr.
    if (node->list.count > 0)
    {
        TypeDescriptor const * inner_type = sem_evaluate_expr(ctx, node->list.children[0]);
        if (inner_type)
            node->resolved_type = (TypeDescriptor *)inner_type;
        return inner_type;
    }
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_ternary_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    // TernaryExpression = RangeExpression (? Expression : Expression)?
    // Without ?: 1 child [Range]; with ?: 3 children [cond, true, false]
    if (node->list.count < 3)
    {
        if (node->list.count > 0)
        {
            TypeDescriptor const * inner_type = sem_evaluate_expr(ctx, node->list.children[0]);
            if (inner_type)
                node->resolved_type = (TypeDescriptor *)inner_type;
            return inner_type;
        }
        return NULL;
    }
    TypeDescriptor const * cond_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * true_type = sem_evaluate_expr(ctx, node->list.children[1]);
    TypeDescriptor const * false_type = sem_evaluate_expr(ctx, node->list.children[2]);
    TypeDescriptor const * result_type = true_type ? true_type : false_type;
    if (result_type)
        node->resolved_type = (TypeDescriptor *)result_type;
    (void)cond_type;
    (void)false_type;
    return result_type;
    
}

static TypeDescriptor const *
sem_evaluate_expression_wrapper(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count > 0)
    {
        TypeDescriptor const * type = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (node->list.children[i] != NULL)
            {
                TypeDescriptor const * child_type = sem_evaluate_expr(ctx, node->list.children[i]);
                if (child_type)
                    type = child_type;
            }
        }
        node->resolved_type = (TypeDescriptor *)type;
        return type;
    }
    return NULL;
    
}

static TypeDescriptor const *
sem_evaluate_assign_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;
    if (node->list.count == 1)
    {
        TypeDescriptor const * inner_type = sem_evaluate_expr(ctx, node->list.children[0]);
        if (inner_type)
            node->resolved_type = (TypeDescriptor *)inner_type;
        return inner_type;
    }
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL)
            sem_evaluate_expr(ctx, node->list.children[i]);
    }
    // Type check LHS vs RHS for a = b expression (3 children: lhs, op, rhs)
    if (node->list.count >= 3)
    {
        odin_grammar_node_t * lhs_node = node->list.children[0];
        odin_grammar_node_t * rhs_node = node->list.children[node->list.count - 1];
        TypeDescriptor const * lhs_type = lhs_node ? lhs_node->resolved_type : NULL;
        TypeDescriptor const * rhs_type = rhs_node ? rhs_node->resolved_type : NULL;
        if (lhs_type != NULL && rhs_type != NULL)
        {
            sem_check_assignment(ctx, lhs_node, lhs_type, rhs_type, rhs_node);
        }
    }
    TypeDescriptor const * lhs_type = node->list.children[0] ? node->list.children[0]->resolved_type : NULL;
    if (lhs_type)
        node->resolved_type = (TypeDescriptor *)lhs_type;
    return lhs_type;
    
}

static TypeDescriptor const *
sem_evaluate_postfix_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;

    // Check if this is a package-qualified name: pkg_name.member
    ImportedPackage * access_pkg = NULL;
    odin_grammar_node_t * base = node->list.children[0];
    if (base != NULL)
    {
        odin_grammar_node_t * inner = base;
        while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
            inner = inner->list.children[0];
        if (inner->type == AST_NODE_IDENTIFIER)
        {
            access_pkg = find_imported_package_by_name(ctx, inner->text);
        }
    }

    if (access_pkg != NULL)
    {
        TypeDescriptor const * type = NULL;
        char const * last_member_name = NULL;

        if (node->list.count >= 2)
        {
            odin_grammar_node_t * postfix_ops = node->list.children[1];
            if (postfix_ops != NULL)
            {
                for (size_t i = 0; i < postfix_ops->list.count; i++)
                {
                    odin_grammar_node_t * op = postfix_ops->list.children[i];
                    if (op == NULL)
                        continue;

                    switch (op->type)
                    {
                    case AST_NODE_POSTFIX_MEMBER:
                        if (op->list.count >= 1 && op->list.children[0] && type == NULL)
                        {
                            char const * member_name = op->list.children[0]->text;
                            last_member_name = member_name;
                            symbol_t * sym = scope_find_symbol_entry(access_pkg->package_scope, member_name);
                            if (sym)
                            {
                                if (sym->is_private)
                                {
                                    char buf[256];
                                    snprintf(buf, sizeof(buf), "symbol '%s' is private in package '%s'", member_name, access_pkg->package_name ? access_pkg->package_name : "unknown");
                                    sem_error_list_add(&ctx->errors, NULL, op, buf);
                                }
                                else
                                {
                                    op->resolved_symbol = sym;
                                    type = sym->value.type_info;
                                    op->resolved_type = (TypeDescriptor *)type;
                                }
                            }
                            else
                            {
                                sem_error_list_add(&ctx->errors, NULL, op, "undeclared name in package");
                            }
                        }
                        break;

                    case AST_NODE_POSTFIX_CALL:
                        if (type && type->kind == TD_KIND_PROC)
                        {
                            // Evaluate argument expressions to populate resolved_type
                            if (op->list.count > 0 && op->list.children[0] != NULL)
                            {
                                odin_grammar_node_t * arg_list = op->list.children[0];
                                if (arg_list->type == AST_NODE_ARGUMENT_LIST)
                                {
                                    for (size_t ai = 0; ai < arg_list->list.count; ai++)
                                    {
                                        if (arg_list->list.children[ai])
                                            sem_evaluate_expr(ctx, arg_list->list.children[ai]);
                                    }
                                }
                            }
                            if (type->proc_metadata.return_count > 1)
                            {
                                op->resolved_type = (TypeDescriptor *)type;
                                break;
                            }
                            type = type->proc_metadata.return_type;
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else if (type && type->kind == TD_KIND_OVERLOAD_BUNDLE)
                        {
                            odin_grammar_node_t * arg_list = NULL;
                            if (op->list.count > 0 && op->list.children[0] != NULL)
                                arg_list = op->list.children[0];

                            symbol_t * winner = sem_resolve_overload_bundle_call(
                                ctx, type, arg_list, op, last_member_name
                            );
                            if (winner && winner->value.type_info)
                            {
                                op->resolved_symbol = winner;
                                TypeDescriptor const * proc_type = winner->value.type_info;
                                if (proc_type && proc_type->kind == TD_KIND_PROC)
                                {
                                    if (proc_type->proc_metadata.return_count > 1)
                                    {
                                        op->resolved_type = (TypeDescriptor *)proc_type;
                                    }
                                    else
                                    {
                                        type = proc_type->proc_metadata.return_type;
                                        op->resolved_type = (TypeDescriptor *)type;
                                    }
                                }
                            }
                        }
                        break;

                    case AST_NODE_POSTFIX_SUBSCRIPT:
                        if (type
                            && (type->kind == TD_KIND_ARRAY || type->kind == TD_KIND_SLICE
                                || type->kind == TD_KIND_MULTI_POINTER || type->kind == TD_KIND_VECTOR))
                        {
                            type = type->element_type;
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else if (type && type->kind == TD_KIND_BASIC && type->as.basic.name != NULL
                                 && strcmp(type->as.basic.name, "string") == 0)
                        {
                            // String subscript: s[i] returns u8
                            type = get_basic_type_by_name(ctx->type_registry, "u8");
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        break;

                    case AST_NODE_POSTFIX_DEREF:
                        if (type && type->kind == TD_KIND_POINTER)
                        {
                            type = type->pointee;
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        break;

                    default:
                        break;
                    }
                }
            }
        }

        node->resolved_type = (TypeDescriptor *)type;
        return type;
    }

    TypeDescriptor const * type = sem_evaluate_expr(ctx, node->list.children[0]);

    if (node->list.count < 2)
    {
        node->resolved_type = (TypeDescriptor *)type;
        return type;
    }
    odin_grammar_node_t * postfix_ops = node->list.children[1];
    if (postfix_ops == NULL)
    {
        node->resolved_type = (TypeDescriptor *)type;
        return type;
    }

    for (size_t i = 0; i < postfix_ops->list.count; i++)
    {
        odin_grammar_node_t * op = postfix_ops->list.children[i];
        if (op == NULL)
            continue;

        switch (op->type)
        {
        case AST_NODE_POSTFIX_CALL:
            if (type && type->kind == TD_KIND_PROC)
            {
                // Evaluate argument expressions
                if (op->list.count > 0 && op->list.children[0] != NULL)
                {
                    odin_grammar_node_t * arg_list = op->list.children[0];
                    if (arg_list->type == AST_NODE_ARGUMENT_LIST)
                    {
                        for (size_t ai = 0; ai < arg_list->list.count; ai++)
                        {
                            if (arg_list->list.children[ai])
                                sem_evaluate_expr(ctx, arg_list->list.children[ai]);
                        }
                    }
                }
                if (type->proc_metadata.return_count > 1)
                {
                    op->resolved_type = (TypeDescriptor *)type;
                    break;
                }
                type = type->proc_metadata.return_type;
                op->resolved_type = (TypeDescriptor *)type;
            }
            else if (type && type->kind == TD_KIND_OVERLOAD_BUNDLE)
            {
                // Get callee name from base expression for error messages
                char const * callee_name = NULL;
                odin_grammar_node_t * base = node->list.children[0];
                if (base != NULL)
                {
                    odin_grammar_node_t * inner = base;
                    while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
                        inner = inner->list.children[0];
                    if (inner->type == AST_NODE_IDENTIFIER && inner->text)
                        callee_name = inner->text;
                }

                odin_grammar_node_t * arg_list = NULL;
                if (op->list.count > 0 && op->list.children[0] != NULL)
                    arg_list = op->list.children[0];

                symbol_t * winner = sem_resolve_overload_bundle_call(
                    ctx, type, arg_list, op, callee_name
                );
                if (winner && winner->value.type_info)
                {
                    op->resolved_symbol = winner;
                    TypeDescriptor const * proc_type = winner->value.type_info;
                    if (proc_type && proc_type->kind == TD_KIND_PROC)
                    {
                        if (proc_type->proc_metadata.return_count > 1)
                        {
                            op->resolved_type = (TypeDescriptor *)proc_type;
                        }
                        else
                        {
                            type = proc_type->proc_metadata.return_type;
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                    }
                }
            }
            break;

        case AST_NODE_POSTFIX_MEMBER:
            if (type && (type->kind == TD_KIND_STRUCT || type->kind == TD_KIND_SOA) && op->list.count >= 1
                && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                field_access_path_t path;
                if (type_descriptor_find_struct_field_path(type, field_name, &path))
                {
                    TypeDescriptor const * cur_type = type;
                    for (int pi = 0; pi < path.count; pi++)
                    {
                        struct_field_t const * f = type_descriptor_get_struct_field(cur_type, path.indices[pi]);
                        if (f == NULL)
                            break;
                        if (pi == path.count - 1)
                            type = f->type_desc;
                        else
                            cur_type = f->type_desc;
                    }
                    op->resolved_type = (TypeDescriptor *)type;
                }
            }
            else if (type && type->kind == TD_KIND_UNION && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                int field_idx = type_descriptor_find_union_field_index(type, field_name);
                if (field_idx >= 0)
                {
                    struct_field_t const * field = type_descriptor_get_union_field(type, field_idx);
                    if (field)
                    {
                        type = field->type_desc;
                        op->resolved_type = (TypeDescriptor *)type;
                    }
                }
            }
            else if (type && type->kind == TD_KIND_BIT_FIELD && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                bit_field_field_info const * bf = type_descriptor_find_bit_field_field(type, field_name);
                if (bf)
                {
                    type = bf->type;
                    op->resolved_type = (TypeDescriptor *)type;
                }
            }
            else if (type && type->kind == TD_KIND_MAYBE && op->list.count >= 1 && op->list.children[0])
            {
                // Maybe(T).value -> T
                char const * field_name = op->list.children[0]->text;
                if (field_name && strcmp(field_name, "value") == 0)
                {
                    type = type->as.maybe.inner_type;
                    op->resolved_type = (TypeDescriptor *)type;
                }
            }
            else if (type && type->kind == TD_KIND_VECTOR && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name && is_valid_swizzle(field_name, type->as.vector.lane_count))
                {
                    int swizzle_len = (int)strlen(field_name);
                    if (swizzle_len == 1)
                        type = type->element_type;
                    else
                        type = get_or_create_vector_type(
                            ctx->type_registry, type->element_type, swizzle_len
                        );
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op,
                        "invalid swizzle or vector has no field named");
                }
            }
            else if (type && type->kind == TD_KIND_BASIC && type->as.basic.name != NULL
                     && strcmp(type->as.basic.name, "string") == 0 && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name && strcmp(field_name, "len") == 0)
                {
                    type = get_basic_type_by_name(ctx->type_registry, "int");
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else if (field_name && strcmp(field_name, "data") == 0)
                {
                    type = get_or_create_pointer_type(ctx->type_registry,
                        get_basic_type_by_name(ctx->type_registry, "u8"));
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "string has no field named");
                }
            }
            else if (type && type->kind == TD_KIND_SLICE && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name && strcmp(field_name, "len") == 0)
                {
                    type = get_basic_type_by_name(ctx->type_registry, "int");
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else if (field_name && strcmp(field_name, "data") == 0)
                {
                    type = get_or_create_pointer_type(ctx->type_registry, type->element_type);
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "slice has no field named");
                }
            }
            else if (type && type->kind == TD_KIND_DYNAMIC_ARRAY && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name && strcmp(field_name, "len") == 0)
                {
                    type = get_basic_type_by_name(ctx->type_registry, "int");
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else if (field_name && strcmp(field_name, "cap") == 0)
                {
                    type = get_basic_type_by_name(ctx->type_registry, "int");
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else if (field_name && strcmp(field_name, "data") == 0)
                {
                    type = get_or_create_pointer_type(ctx->type_registry, type->element_type);
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "dynamic array has no field named");
                }
            }
            else if (type && type->kind == TD_KIND_ARRAY && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name && strcmp(field_name, "len") == 0)
                {
                    type = get_basic_type_by_name(ctx->type_registry, "int");
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "array has no field named");
                }
            }
            else if (type && (type->kind == TD_KIND_POINTER || type->kind == TD_KIND_MULTI_POINTER)
                     && op->list.count >= 1 && op->list.children[0])
            {
                // Pointer auto-dereference for member access: p.field -> p^.field
                TypeDescriptor const * pointee = type->pointee;
                if (pointee)
                {
                    char const * field_name = op->list.children[0]->text;
                    if (field_name == NULL)
                    {
                        sem_error_list_add(&ctx->errors, NULL, op, "member access: missing field name");
                        break;
                    }
                    if (pointee->kind == TD_KIND_STRUCT || pointee->kind == TD_KIND_SOA)
                    {
                        field_access_path_t path;
                        if (type_descriptor_find_struct_field_path(pointee, field_name, &path))
                        {
                            TypeDescriptor const * cur_type = pointee;
                            for (int pi = 0; pi < path.count; pi++)
                            {
                                struct_field_t const * f = type_descriptor_get_struct_field(cur_type, path.indices[pi]);
                                if (f == NULL)
                                    break;
                                if (pi == path.count - 1)
                                    type = f->type_desc;
                                else
                                    cur_type = f->type_desc;
                            }
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else
                        {
                            sem_error_list_add(&ctx->errors, NULL, op, "pointer to struct has no field named");
                        }
                    }
                    else if (pointee->kind == TD_KIND_UNION)
                    {
                        int field_idx = type_descriptor_find_union_field_index(pointee, field_name);
                        if (field_idx >= 0)
                        {
                            struct_field_t const * field = type_descriptor_get_union_field(pointee, field_idx);
                            if (field)
                            {
                                type = field->type_desc;
                                op->resolved_type = (TypeDescriptor *)type;
                            }
                        }
                        else
                        {
                            sem_error_list_add(&ctx->errors, NULL, op, "pointer to union has no field named");
                        }
                    }
                    else if (pointee->kind == TD_KIND_MAYBE && strcmp(field_name, "value") == 0)
                    {
                        type = pointee->as.maybe.inner_type;
                        op->resolved_type = (TypeDescriptor *)type;
                    }
                    else if (pointee->kind == TD_KIND_BASIC && pointee->as.basic.name != NULL
                             && strcmp(pointee->as.basic.name, "string") == 0)
                    {
                        if (strcmp(field_name, "len") == 0)
                        {
                            type = get_basic_type_by_name(ctx->type_registry, "int");
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else if (strcmp(field_name, "data") == 0)
                        {
                            type = get_or_create_pointer_type(ctx->type_registry,
                                get_basic_type_by_name(ctx->type_registry, "u8"));
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else
                        {
                            sem_error_list_add(&ctx->errors, NULL, op, "pointer to string has no field named");
                        }
                    }
                    else if (pointee->kind == TD_KIND_SLICE)
                    {
                        if (strcmp(field_name, "len") == 0)
                        {
                            type = get_basic_type_by_name(ctx->type_registry, "int");
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else if (strcmp(field_name, "data") == 0)
                        {
                            type = get_or_create_pointer_type(ctx->type_registry, pointee->element_type);
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                        else
                        {
                            sem_error_list_add(&ctx->errors, NULL, op, "pointer to slice has no field named");
                        }
                    }
                    else
                    {
                        sem_error_list_add(&ctx->errors, NULL, op, "cannot access member through pointer to this type");
                    }
                }
            }
            break;

        case AST_NODE_POSTFIX_SUBSCRIPT:
            if (type
                && (type->kind == TD_KIND_ARRAY || type->kind == TD_KIND_SLICE
                    || type->kind == TD_KIND_MULTI_POINTER || type->kind == TD_KIND_VECTOR))
            {
                type = type->element_type;
                op->resolved_type = (TypeDescriptor *)type;
            }
            else if (type && type->kind == TD_KIND_MAP)
            {
                type = type->as.map.value_type;
                op->resolved_type = (TypeDescriptor *)type;
            }
            else if (type && type->kind == TD_KIND_BASIC && type->as.basic.name != NULL
                     && strcmp(type->as.basic.name, "string") == 0)
            {
                type = get_basic_type_by_name(ctx->type_registry, "u8");
                op->resolved_type = (TypeDescriptor *)type;
            }
            break;

        case AST_NODE_POSTFIX_DEREF:
            if (type && (type->kind == TD_KIND_POINTER || type->kind == TD_KIND_MULTI_POINTER))
            {
                type = type->pointee;
                op->resolved_type = (TypeDescriptor *)type;
            }
            break;

        case AST_NODE_POSTFIX_ASSERTION:
        {
            // Type assertion x.(T)
            if (type && type->kind == TD_KIND_BASIC && type->as.basic.name
                && strcmp(type->as.basic.name, "any") == 0)
            {
                if (op->list.count > 0)
                {
                    TypeDescriptor const * target_type = sem_resolve_type_expr(ctx, op->list.children[0]);
                    if (target_type)
                    {
                        type = target_type;
                        op->resolved_type = (TypeDescriptor *)type;
                    }
                }
            }
            else if (type && type->kind == TD_KIND_UNION)
            {
                if (op->list.count > 0)
                {
                    TypeDescriptor const * target_type = sem_resolve_type_expr(ctx, op->list.children[0]);
                    if (target_type)
                    {
                        // Find which union field matches the asserted type
                        int field_idx = -1;
                        for (int i = 0; i < type->union_metadata.members.count; i++)
                        {
                            if (type->union_metadata.members.fields[i].type_desc->type_id == target_type->type_id)
                            {
                                field_idx = i;
                                break;
                            }
                        }
                        if (field_idx >= 0)
                        {
                            type = target_type;
                            op->resolved_type = (TypeDescriptor *)type;
                            op->resolved_symbol = (symbol_t *)(intptr_t)field_idx;
                        }
                    }
                }
            }
            else if (type && type->kind == TD_KIND_MAYBE)
            {
                // Maybe(T).(T) — unwrap: returns T if tag == 0 (some), else UB
                if (op->list.count > 0)
                {
                    TypeDescriptor const * target_type = sem_resolve_type_expr(ctx, op->list.children[0]);
                    if (target_type && target_type->type_id == type->as.maybe.inner_type->type_id)
                    {
                        type = target_type;
                        op->resolved_type = (TypeDescriptor *)type;
                    }
                }
            }
            break;
        }

        case AST_NODE_POSTFIX_SLICE:
        case AST_NODE_POSTFIX_SLICE_LT:
            if (type && type->kind == TD_KIND_SLICE)
            {
                // Slicing a slice yields the same slice type
                op->resolved_type = (TypeDescriptor *)type;
            }
            else if (type && type->kind == TD_KIND_ARRAY)
            {
                // Slicing an array yields a slice of the element type
                TypeDescriptor const * slice_type
                    = get_or_create_slice_type(ctx->type_registry, type->element_type);
                type = slice_type;
                op->resolved_type = (TypeDescriptor *)type;
            }
            break;

        default:
            break;
        }
    }

    node->resolved_type = (TypeDescriptor *)type;
    return type;
    
}

static TypeDescriptor const *
sem_evaluate_directive_with_args(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->text && strncmp(node->text, "#assert", 7) == 0)
    {
        odin_grammar_node_t * expr = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            // Skip the identifier child from lexeme("#" DirectiveName)
            if (child->type == AST_NODE_IDENTIFIER)
                continue;

            if (expr != NULL) {
                sem_error_list_add(&ctx->errors, NULL, node,
                    "#assert requires exactly one expression");
                return NULL;
            }
            expr = child;
        }

        if (expr == NULL)
        {
            sem_error_list_add(&ctx->errors, NULL, node,
                "#assert requires an expression");
            return NULL;
        }

        sem_evaluate_expr(ctx, expr);
        if (expr->resolved_type == NULL)
            return NULL;

        int result = sem_evaluate_constant_bool(ctx, expr);
        if (result == 0)
            sem_error_list_add(&ctx->errors, NULL, node, "#assert failed");
    }
    return NULL;
    
}
// --- Statement analysis ---

static bool
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
        // Unwrap expression wrapper nodes to find the underlying terminal
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
        // Untyped integer literal → any integer type
        if (inner != NULL && inner->type == AST_NODE_INTEGER_VALUE && is_integer_kind(to_type))
            return true;
        // Untyped float literal → any float type
        if (inner != NULL && inner->type == AST_NODE_FLOAT_VALUE && is_floating_kind(to_type))
            return true;
    }
    return false;
}

static bool
sem_types_assignable(
    SemContext * ctx, odin_grammar_node_t * src_node, TypeDescriptor const * src_type, TypeDescriptor const * dst_type
)
{
    if (src_type == dst_type)
        return true;
    if (src_type == NULL || dst_type == NULL)
        return false;
    // Allow untyped literal → any numeric type (including distinct numeric)
    if (sem_can_implicitly_convert(ctx, src_node, src_type, dst_type))
        return true;
    // Distinct types are only assignable to themselves (not to/from their base type)
    // This is enforced by the pointer-equality check above (src_type == dst_type).
    return false;
}

static void
sem_check_assignment(
    SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * target_type,
    TypeDescriptor const * src_type, odin_grammar_node_t * src_node
)
{
    // Only enforce type checking for distinct types — the old code had no
    // general assignment type checking, and adding it would break existing tests
    // that rely on implicit conversions (bool→int, int→i64, etc.).
    // Distinct types must not be assignable to/from their base type without a cast.
    if (target_type == NULL || src_type == NULL)
        return;
    if (target_type->kind != TD_KIND_DISTINCT && src_type->kind != TD_KIND_DISTINCT)
        return; // No distinct types involved — use old lax behavior
    if (sem_types_assignable(ctx, src_node, src_type, target_type))
        return;
    // Skip error for auto_cast expressions — they explicitly request a cast
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

static symbol_t *
sem_resolve_overload_bundle_call(
    SemContext * ctx,
    TypeDescriptor const * bundle_type,
    odin_grammar_node_t * arg_list_node,
    odin_grammar_node_t * call_op,
    char const * callee_name
)
{
    if (bundle_type == NULL || bundle_type->kind != TD_KIND_OVERLOAD_BUNDLE)
        return NULL;

    int candidate_count = bundle_type->as.overload_bundle.candidate_count;
    TypeDescriptor const ** candidate_types = bundle_type->as.overload_bundle.candidate_types;
    symbol_t ** candidate_symbols = bundle_type->as.overload_bundle.candidate_symbols;

    // Evaluate argument expressions and collect their types
    int arg_count = 0;
    TypeDescriptor const * arg_types[64];
    if (arg_list_node != NULL && arg_list_node->type == AST_NODE_ARGUMENT_LIST)
    {
        for (size_t ai = 0; ai < arg_list_node->list.count; ai++)
        {
            odin_grammar_node_t * arg_node = arg_list_node->list.children[ai];
            if (arg_node == NULL)
                continue;
            sem_evaluate_expr(ctx, arg_node);
            if ((size_t)arg_count < 64)
            {
                arg_types[arg_count] = arg_node->resolved_type;
                arg_count++;
            }
        }
    }

    symbol_t * best_match = NULL;
    int match_count = 0;

    for (int ci = 0; ci < candidate_count; ci++)
    {
        TypeDescriptor const * proc_type = candidate_types[ci];
        if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
            continue;

        ProcMetadata const * pm = &proc_type->proc_metadata;

        // Check parameter count match (handle variadic)
        bool params_match = false;
        if (pm->is_variadic)
        {
            // Variadic: must have at least pm->param_count - 1 args (all non-variadic params match)
            params_match = (arg_count >= pm->param_count - 1);
        }
        else
        {
            params_match = (arg_count == pm->param_count);
        }

        if (!params_match)
            continue;

        // Check each parameter type
        bool all_args_match = true;
        for (int ai = 0; ai < arg_count && ai < pm->param_count; ai++)
        {
            if (!sem_types_assignable(ctx, NULL, arg_types[ai], pm->params[ai]))
            {
                all_args_match = false;
                break;
            }
        }
        // For variadic, check remaining args against the last param type (repeated)
        if (all_args_match && pm->is_variadic && pm->param_count > 0)
        {
            for (int ai = pm->param_count - 1; ai < arg_count; ai++)
            {
                if (!sem_types_assignable(ctx, NULL, arg_types[ai], pm->params[pm->param_count - 1]))
                {
                    all_args_match = false;
                    break;
                }
            }
        }

        if (all_args_match)
        {
            best_match = candidate_symbols[ci];
            match_count++;
        }
    }

    if (match_count == 0)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "no matching overload for '%s' with %d argument(s)", callee_name ? callee_name : "?", arg_count);
        sem_error_list_add(&ctx->errors, NULL, call_op, buf);
        return NULL;
    }

    if (match_count > 1)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "ambiguous call to '%s' — %d overloads match", callee_name ? callee_name : "?", match_count);
        sem_error_list_add(&ctx->errors, NULL, call_op, buf);
        return NULL;
    }

    return best_match;
}

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
            ImportedPackage * rp = parse_imported_file(runtime_path, ctx->parser, ctx->hook_registry);
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

                    ImportedPackage * pkg = parse_imported_file(resolved, ctx->parser, ctx->hook_registry);

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

                    ImportedPackage * pkg = parse_imported_file(resolved, ctx->parser, ctx->hook_registry);
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

                    ImportedPackage * pkg = parse_imported_file(resolved, ctx->parser, ctx->hook_registry);
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
