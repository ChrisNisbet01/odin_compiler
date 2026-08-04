#include "sem_evaluate_expr.h"

#include "ast_utils.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include "polymorphism.h"
#include "sem_check.h"
#include "sem_context.h"
#include "sem_type_resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration for overload bundle resolution
static symbol_t * sem_resolve_overload_bundle_call(
    SemContext * ctx,
    TypeDescriptor const * bundle_type,
    odin_grammar_node_t * arg_list_node,
    odin_grammar_node_t * call_op,
    char const * callee_name
);

// --- Forward declarations for dispatch table ---
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
static TypeDescriptor const * sem_evaluate_append_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_expand_values_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_compress_values_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_soa_zip_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_soa_unzip_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_struct_lit_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_array_lit_expr(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_evaluate_matrix_lit_expr(SemContext * ctx, odin_grammar_node_t * node);
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
static TypeDescriptor const * sem_evaluate_directive_expr(SemContext * ctx, odin_grammar_node_t * node);

// --- Dispatch table ---

static TypeDescriptor const * (* const sem_evaluate_dispatch[])(SemContext *, odin_grammar_node_t *) = {
    [AST_NODE_INTEGER_VALUE] = sem_evaluate_integer_value,
    [AST_NODE_FLOAT_VALUE] = sem_evaluate_float_value,
    [AST_NODE_STRING_LITERAL] = sem_evaluate_string_literal,
    [AST_NODE_RAW_STRING_LITERAL] = sem_evaluate_string_literal,
    [AST_NODE_RUNE_LITERAL] = sem_evaluate_rune_literal,
    [AST_NODE_BOOL_TRUE] = sem_evaluate_bool_value,
    [AST_NODE_BOOL_FALSE] = sem_evaluate_bool_value,
    [AST_NODE_AUTO_CAST_EXPR] = sem_evaluate_auto_cast_expr,
    [AST_NODE_CAST_EXPR] = sem_evaluate_cast_expr,
    [AST_NODE_TRANSMUTE_EXPR] = sem_evaluate_cast_expr,
    [AST_NODE_LEN_EXPR] = sem_evaluate_len_cap_expr,
    [AST_NODE_CAP_EXPR] = sem_evaluate_len_cap_expr,
    [AST_NODE_MAKE_EXPR] = sem_evaluate_make_expr,
    [AST_NODE_NEW_EXPR] = sem_evaluate_new_expr,
    [AST_NODE_DELETE_EXPR] = sem_evaluate_delete_expr,
    [AST_NODE_APPEND_EXPR] = sem_evaluate_append_expr,
    [AST_NODE_EXPAND_VALUES_EXPR] = sem_evaluate_expand_values_expr,
    [AST_NODE_COMPRESS_VALUES_EXPR] = sem_evaluate_compress_values_expr,
    [AST_NODE_SOA_ZIP_EXPR] = sem_evaluate_soa_zip_expr,
    [AST_NODE_SOA_UNZIP_EXPR] = sem_evaluate_soa_unzip_expr,
    [AST_NODE_STRUCT_LIT_EXPR] = sem_evaluate_struct_lit_expr,
    [AST_NODE_ARRAY_LIT_EXPR] = sem_evaluate_array_lit_expr,
    [AST_NODE_MATRIX_LIT_EXPR] = sem_evaluate_matrix_lit_expr,
    [AST_NODE_INCL_EXPR] = sem_evaluate_incl_excl_expr,
    [AST_NODE_EXCL_EXPR] = sem_evaluate_incl_excl_expr,
    [AST_NODE_COMPLEX_EXPR] = sem_evaluate_complex_quaternion_expr,
    [AST_NODE_QUATERNION_EXPR] = sem_evaluate_complex_quaternion_expr,
    [AST_NODE_SIZE_OF_EXPR] = sem_evaluate_size_align_of_expr,
    [AST_NODE_ALIGN_OF_EXPR] = sem_evaluate_size_align_of_expr,
    [AST_NODE_OFFSET_OF_EXPR] = sem_evaluate_offset_of_expr,
    [AST_NODE_RAW_DATA_EXPR] = sem_evaluate_raw_data_expr,
    [AST_NODE_TYPE_OF_EXPR] = sem_evaluate_type_of_expr,
    [AST_NODE_TYPEID_OF_EXPR] = sem_evaluate_typeid_of_expr,
    [AST_NODE_TYPE_INFO_OF_EXPR] = sem_evaluate_type_info_of_expr,
    [AST_NODE_MIN_EXPR] = sem_evaluate_min_max_expr,
    [AST_NODE_MAX_EXPR] = sem_evaluate_min_max_expr,
    [AST_NODE_DISTINCT_TYPE] = sem_evaluate_distinct_type,
    [AST_NODE_NIL] = sem_evaluate_nil,
    [AST_NODE_DIRECTIVE] = sem_evaluate_directive,
    [AST_NODE_DIRECTIVE_EXPR] = sem_evaluate_directive_expr,
    [AST_NODE_CONTEXT_EXPR] = sem_evaluate_context_expr,
    [AST_NODE_IDENTIFIER] = sem_evaluate_identifier,
    [AST_NODE_UNARY_EXPRESSION] = sem_evaluate_unary_expr,
    [AST_NODE_RANGE_EXPRESSION] = sem_evaluate_range_expr,
    [AST_NODE_MUL_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_ADD_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_SHIFT_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_BIT_AND_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_BIT_XOR_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_BIT_OR_EXPRESSION] = sem_evaluate_binary_arith_expr,
    [AST_NODE_COMP_EXPRESSION] = sem_evaluate_comp_log_expr,
    [AST_NODE_LOG_AND_EXPRESSION] = sem_evaluate_comp_log_expr,
    [AST_NODE_LOG_OR_EXPRESSION] = sem_evaluate_comp_log_expr,
    [AST_NODE_POSTFIX_CALL] = sem_evaluate_postfix_call,
    [AST_NODE_POSTFIX_MEMBER] = sem_evaluate_postfix_member,
    [AST_NODE_OR_ELSE] = sem_evaluate_or_else,
    [AST_NODE_OR_RETURN] = sem_evaluate_or_return,
    [AST_NODE_TERNARY_EXPRESSION] = sem_evaluate_ternary_expr,
    [AST_NODE_EXPRESSION] = sem_evaluate_expression_wrapper,
    [AST_NODE_PRIMARY_EXPRESSION] = sem_evaluate_expression_wrapper,
    [AST_NODE_ASSIGN_EXPRESSION] = sem_evaluate_assign_expr,
    [AST_NODE_POSTFIX_EXPRESSION] = sem_evaluate_postfix_expr,
    [AST_NODE_DIRECTIVE_WITH_ARGS] = sem_evaluate_directive_with_args,
};

// --- sem_evaluate_expr (central dispatch) ---

TypeDescriptor const *
sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;
    if ((size_t)node->type < AST_NODE_COUNT && sem_evaluate_dispatch[node->type])
        return sem_evaluate_dispatch[node->type](ctx, node);
    return NULL;
}

// --- Extracted case functions ---

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
sem_evaluate_implicit_cast_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    (void)ctx;
    (void)node;
    return NULL;
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
    
    // For maps: evaluate optional allocator argument (children[2])
    // For slices/DAs: children[2] is cap (optional), evaluate it
    if (node->list.count >= 3)
    {
        odin_grammar_node_t * third_arg = node->list.children[2];
        sem_evaluate_expr(ctx, third_arg);
    }
    
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
sem_evaluate_append_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "append requires at least 2 arguments");
        return NULL;
    }
    odin_grammar_node_t * arr_node = node->list.children[0];
    sem_evaluate_expr(ctx, arr_node);
    TypeDescriptor const * arr_type = arr_node->resolved_type;
    if (arr_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "append: first argument has no type");
        return NULL;
    }
    if (arr_type->kind != TD_KIND_DYNAMIC_ARRAY)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "append: first argument must be a [dynamic] array");
        return NULL;
    }
    TypeDescriptor const * elem_type = arr_type->element_type;
    if (elem_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "append: element type is NULL");
        return NULL;
    }
    for (size_t i = 1; i < node->list.count; i++)
    {
        sem_evaluate_expr(ctx, node->list.children[i]);
    }
    node->resolved_type = (TypeDescriptor *)arr_type;
    return arr_type;
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
    int actual_count = (int)node->list.count - 1;
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

    odin_grammar_node_t * arg_list = (node->list.count >= 1) ? node->list.children[0] : NULL;
    odin_grammar_node_t * arg_expr = (arg_list && arg_list->list.count >= 1) ? arg_list->list.children[0] : NULL;
    if (arg_expr == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "soa_zip requires at least one argument");
        node->resolved_type = NULL;
        return NULL;
    }

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
        elem_types[i] = field->type_desc;
    }
    TypeDescriptor const * tuple_type = get_or_create_tuple_type(ctx->type_registry, elem_types, field_count);
    free(elem_types);
    node->resolved_type = (TypeDescriptor *)tuple_type;
    return tuple_type;
    
}

// --- Poly struct type inference from struct literal field values ---
// When `Box{val=42}` is used without explicit type arguments, infer the
// poly type parameters by matching the field values against the struct
// definition's field types.
static TypeDescriptor const *
sem_infer_poly_struct_type(
    SemContext * ctx,
    odin_grammar_node_t * type_node,  // the bare Identifier (e.g. "Box")
    odin_grammar_node_t * lit_node     // the StructLitExpr node
)
{
    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), type_node->text);
    if (sym == NULL || !sym->is_polymorphic || sym->kind != SYMBOL_TYPE)
        return NULL;

    odin_grammar_node_t * origin = poly_get_origin(sym);
    if (origin == NULL)
        return NULL;

    // Extract the StructType from the ConstantDecl
    odin_grammar_node_t * struct_type = NULL;
    for (size_t i = 0; i < origin->list.count; i++)
    {
        odin_grammar_node_t * child = origin->list.children[i];
        if (child != NULL && child->type == AST_NODE_STRUCT_TYPE)
        {
            struct_type = child;
            break;
        }
    }
    if (struct_type == NULL)
        return NULL;

    // Extract the ParameterList
    odin_grammar_node_t * param_list = node_find_child(struct_type, AST_NODE_PARAMETER_LIST);
    if (param_list == NULL)
        return NULL;

    // Collect individual PARAMETER nodes
    odin_grammar_node_t * params[32];
    int param_count = collect_parameters_from_param_list(param_list, params, 32);
    if (param_count == 0)
        return NULL;

    // Collect struct field definitions from the StructType body.
    // StructRawBody contains StructFieldList → StructField children.
    // Each StructField has: Identifier(s), TypePrefix (the field type)
    odin_grammar_node_t * field_list = node_find_child(struct_type, AST_NODE_STRUCT_FIELD_LIST);
    if (field_list == NULL)
        return NULL;

    // Collect (field_name, field_type_node) pairs from the struct definition.
    // For multi-name fields like `x, y: int`, each name gets its own entry.
    char const * field_names[64];
    odin_grammar_node_t * field_type_nodes[64];
    int field_def_count = 0;

    for (size_t i = 0; i < field_list->list.count && field_def_count < 64; i++)
    {
        odin_grammar_node_t * field = field_list->list.children[i];
        if (field == NULL || field->type != AST_NODE_STRUCT_FIELD)
            continue;

        odin_grammar_node_t * names[16];
        int name_count = 0;
        odin_grammar_node_t * ftype = NULL;

        for (size_t ci = 0; ci < field->list.count; ci++)
        {
            odin_grammar_node_t * child = field->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_IDENTIFIER && name_count < 16)
                names[name_count++] = child;
            else if (is_type_node(child) || child->type == AST_NODE_DIRECTIVE)
            {
                if (child->type == AST_NODE_DIRECTIVE && child->text
                    && strcmp(child->text, "#align") == 0)
                {
                    ci++; // skip #align's integer argument
                    continue;
                }
                ftype = child;
            }
            else if (child->type == AST_NODE_IDENTIFIER && name_count > 0 && ftype == NULL)
            {
                // Bare identifier type (e.g. T in poly struct) — only if no
                // other type node was found, the last identifier is the type
                ftype = child;
            }
        }

        // If all children were identifiers, the last one is the type
        if (ftype == NULL && name_count > 1)
        {
            ftype = names[name_count - 1];
            name_count--;
        }

        for (int n = 0; n < name_count && field_def_count < 64; n++)
        {
            field_names[field_def_count] = names[n]->text;
            field_type_nodes[field_def_count] = ftype;
            field_def_count++;
        }
    }

    // Collect field values from the struct literal.
    // StructLitFields → StructLitField children: [Identifier, AssignExpression]
    odin_grammar_node_t * lit_fields = NULL;
    for (size_t i = 1; i < lit_node->list.count; i++)
    {
        if (lit_node->list.children[i] != NULL
            && lit_node->list.children[i]->type == AST_NODE_STRUCT_LIT_FIELDS)
        {
            lit_fields = lit_node->list.children[i];
            break;
        }
    }

    // Evaluate each field value and store its resolved type
    char const * lit_field_names[64];
    TypeDescriptor const * lit_field_types[64];
    int lit_field_count = 0;

    if (lit_fields != NULL)
    {
        for (size_t i = 0; i < lit_fields->list.count && lit_field_count < 64; i++)
        {
            odin_grammar_node_t * field = lit_fields->list.children[i];
            if (field == NULL || field->type != AST_NODE_STRUCT_LIT_FIELD)
                continue;
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * value_expr = NULL;
            for (size_t ci = 0; ci < field->list.count; ci++)
            {
                odin_grammar_node_t * child = field->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = child;
                else
                    value_expr = child;
            }
            if (name_node == NULL || name_node->text == NULL)
                continue;
            TypeDescriptor const * val_type = NULL;
            if (value_expr != NULL)
                val_type = sem_evaluate_expr(ctx, value_expr);
            lit_field_names[lit_field_count] = name_node->text;
            lit_field_types[lit_field_count] = val_type;
            lit_field_count++;
        }
    }

    // Build the poly environment by matching struct definition fields
    // against literal field values. For each field whose type is a bare
    // Identifier matching a poly param name, bind that param to the
    // literal field value's type.
    PolyEnv env = {0};

    for (int p = 0; p < param_count; p++)
    {
        odin_grammar_node_t * param = params[p];
        // Extract param name (PolyIdent child)
        char const * pname = NULL;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child != NULL && child->type == AST_NODE_POLY_IDENT)
            {
                pname = child->text;
                break;
            }
        }
        if (pname == NULL)
            continue;
        // Strip $ prefix
        if (pname[0] == '$')
            pname++;

        // Skip int params (can't infer int params from field value types)
        bool is_int_param = false;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child != NULL && child->text
                && (strcmp(child->text, "int") == 0 || strcmp(child->text, "i8") == 0
                    || strcmp(child->text, "i16") == 0 || strcmp(child->text, "i32") == 0
                    || strcmp(child->text, "i64") == 0 || strcmp(child->text, "i128") == 0
                    || strcmp(child->text, "u8") == 0 || strcmp(child->text, "u16") == 0
                    || strcmp(child->text, "u32") == 0 || strcmp(child->text, "u64") == 0
                    || strcmp(child->text, "u128") == 0))
            {
                is_int_param = true;
                break;
            }
        }
        if (is_int_param)
            continue;

        // Find a struct definition field whose type is this poly param name
        for (int f = 0; f < field_def_count; f++)
        {
            if (field_type_nodes[f] == NULL)
                continue;
            // The field type must be a bare Identifier matching the param name
            if (field_type_nodes[f]->type != AST_NODE_IDENTIFIER
                || field_type_nodes[f]->text == NULL)
                continue;
            char const * ftype_text = field_type_nodes[f]->text;
            if (strcmp(ftype_text, pname) != 0)
                continue;

            // Find the matching literal field value
            for (int v = 0; v < lit_field_count; v++)
            {
                if (strcmp(lit_field_names[v], field_names[f]) == 0
                    && lit_field_types[v] != NULL)
                {
                    // Bind the poly param to this value's type
                    if (env.count < 16)
                    {
                        env.entries[env.count].name = strdup(pname);
                        env.entries[env.count].kind = POLY_SLOT_TYPE;
                        env.entries[env.count].bound_type = lit_field_types[v];
                        env.count++;
                    }
                    break;
                }
            }
        }
    }

    if (env.count == 0)
        return NULL;

    // Push env and resolve the struct type
    poly_env_push(ctx, &env);
    TypeDescriptor const * result = sem_resolve_struct_type(ctx, struct_type);
    poly_env_pop(ctx);

    // Also check the poly struct dedup cache: if the same instantiation
    // was already resolved via sem_resolve_type_application, reuse it.
    // sem_resolve_struct_type creates a new TypeDescriptor via register_struct_type,
    // but the dedup cache in sem_resolve_type_application maps (origin, args) → TypeDescriptor.
    // For struct literals without explicit type args, the dedup cache won't help because
    // the TypeApplication node doesn't exist in the AST. The new TypeDescriptor will
    // have a different pointer from the one created by sem_resolve_type_application.
    // However, LLVM struct types are deduplicated by layout, so the llvm_type will match.

    if (result != NULL)
        type_node->resolved_type = (TypeDescriptor *)result;
    return result;
}

// --- StructLitExpr: Vec{x = 1, y = 2} or Box(int){val = 42} ---
// Children: [TypeNode (Identifier | TypeApplication), StructLitFields?]
static TypeDescriptor const *
sem_evaluate_struct_lit_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // First child is the type prefix (Identifier or TypeApplication).
    odin_grammar_node_t * type_node = node->list.children[0];
    if (type_node == NULL)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // Resolve the struct type via the type resolver (handles regular
    // structs, poly struct template lookups, and TypeApplication
    // instantiation, e.g. Box(int)).
    TypeDescriptor const * struct_type = sem_resolve_type_expr(ctx, type_node);
    if (struct_type == NULL
        && type_node->type == AST_NODE_IDENTIFIER
        && type_node->text != NULL)
    {
        // Fallback: poly struct type inference from field values.
        // e.g. `b := Box{val=42}` infers T=int (Box is polymorphic,
        // no explicit type arguments provided).
        struct_type = sem_infer_poly_struct_type(ctx, type_node, node);
    }
    if (struct_type == NULL)
    {
        sem_error_list_add(&ctx->errors, ctx->source_file_path, type_node,
            "struct literal: could not resolve struct type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (struct_type->kind != TD_KIND_STRUCT)
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "struct literal: type is not a struct (got kind %d)", struct_type->kind);
        sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
        node->resolved_type = NULL;
        return NULL;
    }

    // Find the optional StructLitFields child (2nd child).
    odin_grammar_node_t * fields_node = NULL;
    for (size_t i = 1; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL
            && node->list.children[i]->type == AST_NODE_STRUCT_LIT_FIELDS)
        {
            fields_node = node->list.children[i];
            break;
        }
    }

    if (fields_node != NULL)
    {
        // Validate each StructLitField against the struct's declared fields.
        for (size_t i = 0; i < fields_node->list.count; i++)
        {
            odin_grammar_node_t * field = fields_node->list.children[i];
            if (field == NULL || field->type != AST_NODE_STRUCT_LIT_FIELD)
                continue;
            // StructLitField children: [Identifier, AssignExpression]
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * value_expr = NULL;
            for (size_t ci = 0; ci < field->list.count; ci++)
            {
                odin_grammar_node_t * child = field->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = child;
                else
                    value_expr = child;
            }
            if (name_node == NULL || name_node->text == NULL)
            {
                sem_error_list_add(&ctx->errors, ctx->source_file_path, field,
                    "struct literal: field is missing name");
                node->resolved_type = NULL;
                return NULL;
            }
            int field_idx = type_descriptor_find_struct_field_index(struct_type, name_node->text);
            if (field_idx < 0)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "struct literal: struct has no field '%s'", name_node->text);
                sem_error_list_add(&ctx->errors, ctx->source_file_path, name_node, buf);
                node->resolved_type = NULL;
                return NULL;
            }
            // Evaluate the field value expression so it has a resolved_type
            // for IR generation. We do not strictly enforce type-matching
            // here — IR-gen will coerce where possible. (Future: add
            // sem_check_assignment for value vs declared field type.)
            if (value_expr != NULL)
                sem_evaluate_expr(ctx, value_expr);
        }
    }

    node->resolved_type = (TypeDescriptor *)struct_type;
    return struct_type;
}

// --- ArrayLitExpr: [3]int{10, 20, 30} ---
// Children: [ArrayType, ArrayLitElements?]
static TypeDescriptor const *
sem_evaluate_array_lit_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // First child is the ArrayType (e.g. [3]int)
    odin_grammar_node_t * type_node = node->list.children[0];
    if (type_node == NULL || type_node->type != AST_NODE_ARRAY_TYPE)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // Resolve the array type to get element type + count
    TypeDescriptor const * array_type = sem_resolve_type_expr(ctx, type_node);
    if (array_type == NULL)
    {
        sem_error_list_add(&ctx->errors, ctx->source_file_path, type_node,
            "array literal: could not resolve array type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (array_type->kind != TD_KIND_ARRAY)
    {
        sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
            "array literal: type is not an array");
        node->resolved_type = NULL;
        return NULL;
    }

    // Find the optional ArrayLitElements child
    odin_grammar_node_t * elements_node = NULL;
    for (size_t i = 1; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL
            && node->list.children[i]->type == AST_NODE_ARRAY_LIT_ELEMENTS)
        {
            elements_node = node->list.children[i];
            break;
        }
    }

    // Evaluate each element expression so IR gen has resolved_type on each
    if (elements_node != NULL)
    {
        for (size_t i = 0; i < elements_node->list.count; i++)
        {
            odin_grammar_node_t * elem = elements_node->list.children[i];
            if (elem != NULL)
                sem_evaluate_expr(ctx, elem);
        }
    }

    node->resolved_type = (TypeDescriptor *)array_type;
    return array_type;
}

// --- MatrixLitExpr: matrix[2,2]int{1, 2, 3, 4} ---
// Children: [MatrixType, MatrixLitElements?]
static TypeDescriptor const *
sem_evaluate_matrix_lit_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // First child is the MatrixType (e.g. matrix[2,2]int)
    odin_grammar_node_t * type_node = node->list.children[0];
    if (type_node == NULL || type_node->type != AST_NODE_MATRIX_TYPE)
    {
        node->resolved_type = NULL;
        return NULL;
    }

    // Resolve the matrix type to get rows, cols, element type
    TypeDescriptor const * mtx_type = sem_resolve_type_expr(ctx, type_node);
    if (mtx_type == NULL)
    {
        sem_error_list_add(&ctx->errors, ctx->source_file_path, type_node,
            "matrix literal: could not resolve matrix type");
        node->resolved_type = NULL;
        return NULL;
    }
    if (mtx_type->kind != TD_KIND_MATRIX)
    {
        sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
            "matrix literal: type is not a matrix");
        node->resolved_type = NULL;
        return NULL;
    }

    long long rows = mtx_type->as.matrix.rows;
    long long cols = mtx_type->as.matrix.columns;
    long long expected_count = rows * cols;

    // Find the optional MatrixLitElements child
    odin_grammar_node_t * elements_node = NULL;
    for (size_t i = 1; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL
            && node->list.children[i]->type == AST_NODE_MATRIX_LIT_ELEMENTS)
        {
            elements_node = node->list.children[i];
            break;
        }
    }

    // Validate element count
    if (elements_node != NULL)
    {
        if ((long long)elements_node->list.count != expected_count)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "matrix literal: expected %lld elements (%lldx%lld matrix), got %zu",
                     expected_count, rows, cols, elements_node->list.count);
            sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
            node->resolved_type = NULL;
            return NULL;
        }

        // Evaluate each element expression so IR gen has resolved_type on each
        for (size_t i = 0; i < elements_node->list.count; i++)
        {
            odin_grammar_node_t * elem = elements_node->list.children[i];
            if (elem != NULL)
                sem_evaluate_expr(ctx, elem);
        }
    }

    node->resolved_type = (TypeDescriptor *)mtx_type;
    return mtx_type;
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
    LLVMContextRef llvm_ctx = ctx->gen_ctx->context;

    // Collect the component value expressions and their field indices.
    // Positional form children are expressions directly (index = position).
    // Named form wraps the fields in a QuaternionFields node.
    odin_grammar_node_t * value_exprs[4];
    odin_grammar_node_t * fields_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL
            && node->list.children[i]->type == AST_NODE_QUATERNION_FIELDS)
        {
            fields_node = node->list.children[i];
            break;
        }
    }

    int arg_count = 0;
    if (fields_node != NULL)
    {
        // Named form: quaternion(w = 1, x = 0, y = 0, z = 0)
        for (size_t i = 0; i < fields_node->list.count && arg_count < 4; i++)
        {
            odin_grammar_node_t * field = fields_node->list.children[i];
            if (field == NULL || field->type != AST_NODE_QUATERNION_FIELD)
                continue;
            odin_grammar_node_t * name_node = NULL;
            odin_grammar_node_t * value_expr = NULL;
            for (size_t ci = 0; ci < field->list.count; ci++)
            {
                odin_grammar_node_t * child = field->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && name_node == NULL)
                    name_node = child;
                else
                    value_expr = child;
            }
            if (name_node == NULL || name_node->text == NULL || value_expr == NULL)
            {
                sem_error_list_add(&ctx->errors, NULL, field,
                    "complex/quaternion: named argument is missing a name or value");
                node->resolved_type = NULL;
                return NULL;
            }
            int field_index = -1;
            if (strcmp(name_node->text, "w") == 0) field_index = 0;
            else if (strcmp(name_node->text, "x") == 0) field_index = 1;
            else if (strcmp(name_node->text, "y") == 0) field_index = 2;
            else if (strcmp(name_node->text, "z") == 0) field_index = 3;
            if (field_index < 0)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "complex/quaternion: invalid component name '%s' (expected w, x, y, or z)",
                    name_node->text);
                sem_error_list_add(&ctx->errors, NULL, name_node, buf);
                node->resolved_type = NULL;
                return NULL;
            }
            value_exprs[field_index] = value_expr;
            arg_count++;
        }
    }
    else
    {
        // Positional form: quaternion(1.0, 2.0, 3.0, 4.0) / complex(a, b)
        for (size_t i = 0; i < node->list.count && arg_count < 4; i++)
        {
            if (node->list.children[i] == NULL)
                continue;
            value_exprs[arg_count] = node->list.children[i];
            arg_count++;
        }
    }

    if (arg_count < min_args)
    {
        sem_error_list_add(&ctx->errors, NULL, node, "complex/quaternion: insufficient arguments");
        node->resolved_type = NULL;
        return NULL;
    }

    // Determine the component float type. Any f16/f32/f64 arg sets the type
    // (all float args must agree); integer-typed args are coerced to it. If
    // every arg is integer-typed, default to f32 (quaternion128 / complex64).
    TypeDescriptor const * component_type = NULL;
    bool saw_integer = false;
    for (int i = 0; i < arg_count; i++)
    {
        TypeDescriptor const * arg = sem_evaluate_expr(ctx, value_exprs[i]);
        if (arg == NULL || arg->llvm_type == NULL)
        {
            node->resolved_type = NULL;
            return NULL;
        }
        if (arg->llvm_type == LLVMHalfTypeInContext(llvm_ctx)
            || arg->llvm_type == LLVMFloatTypeInContext(llvm_ctx)
            || arg->llvm_type == LLVMDoubleTypeInContext(llvm_ctx))
        {
            if (component_type == NULL)
            {
                component_type = arg;
            }
            else if (component_type->llvm_type != arg->llvm_type)
            {
                sem_error_list_add(&ctx->errors, NULL, value_exprs[i],
                    "complex/quaternion: all float arguments must have the same type");
                node->resolved_type = NULL;
                return NULL;
            }
        }
        else if (LLVMGetTypeKind(arg->llvm_type) == LLVMIntegerTypeKind)
        {
            saw_integer = true;
        }
        else
        {
            sem_error_list_add(&ctx->errors, NULL, value_exprs[i],
                "complex/quaternion: arguments must be f16, f32, f64, or integer types");
            node->resolved_type = NULL;
            return NULL;
        }
    }

    if (component_type == NULL)
    {
        if (!saw_integer)
        {
            sem_error_list_add(&ctx->errors, NULL, node,
                "complex/quaternion: arguments must be f16, f32, f64, or integer types");
            node->resolved_type = NULL;
            return NULL;
        }
        component_type = get_basic_type_by_name(ctx->type_registry, "f32");
    }

    char const * target_name = NULL;
    if (component_type->llvm_type == LLVMHalfTypeInContext(llvm_ctx))
        target_name = is_complex ? "complex32" : "quaternion64";
    else if (component_type->llvm_type == LLVMFloatTypeInContext(llvm_ctx))
        target_name = is_complex ? "complex64" : "quaternion128";
    else if (component_type->llvm_type == LLVMDoubleTypeInContext(llvm_ctx))
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
    (void)field_node;
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
        && operand_type->kind != TD_KIND_DYNAMIC_ARRAY
        && !(operand_type->kind == TD_KIND_BASIC && operand_type->as.basic.name != NULL
             && strcmp(operand_type->as.basic.name, "string") == 0))
    {
        sem_error_list_add(&ctx->errors, NULL, node, "raw_data requires a slice, array, dynamic array, or string");
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
sem_evaluate_directive_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    // DirectiveExpr = Directive UnaryExpression. The directive itself is not
    // honoured; the operand is evaluated as normal and its type is propagated.
    if (node->list.count < 2)
        return NULL;
    odin_grammar_node_t * operand = node->list.children[1];
    TypeDescriptor const * t = sem_evaluate_expr(ctx, operand);
    node->resolved_type = (TypeDescriptor *)t;
    return t;
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
    TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
    TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);

    // Check for matrix-related operations
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node)
    {
        AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
        if (op_md == NULL)
        {
            node->resolved_type = (TypeDescriptor *)left_type;
            return left_type;
        }

        // Matrix × Matrix (standard matrix multiplication)
        if (op_md->kind == OP_MUL
            && left_type && left_type->kind == TD_KIND_MATRIX
            && right_type && right_type->kind == TD_KIND_MATRIX)
        {
            if (left_type->as.matrix.columns != right_type->as.matrix.rows)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "matrix multiplication dimension mismatch: cannot multiply matrix[%lld,%lld] by matrix[%lld,%lld]",
                    (long long)left_type->as.matrix.rows, (long long)left_type->as.matrix.columns,
                    (long long)right_type->as.matrix.rows, (long long)right_type->as.matrix.columns);
                sem_error_list_add(&ctx->errors, ctx->source_file_path, op_node, buf);
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            if (left_type->as.matrix.element_type != right_type->as.matrix.element_type)
            {
                sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
                    "matrix multiplication element type mismatch");
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            TypeDescriptor const * result_type = get_or_create_matrix_type(
                ctx->type_registry,
                left_type->as.matrix.rows,
                right_type->as.matrix.columns,
                left_type->as.matrix.element_type,
                left_type->as.matrix.is_row_major
            );
            node->resolved_type = (TypeDescriptor *)result_type;
            return result_type;
        }

        // Matrix × Vector (matrix-vector multiplication)
        if (op_md->kind == OP_MUL
            && left_type && left_type->kind == TD_KIND_MATRIX
            && right_type && right_type->kind == TD_KIND_ARRAY)
        {
            if (left_type->as.matrix.columns != (int64_t)right_type->as.array.count)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "matrix-vector multiplication dimension mismatch: cannot multiply matrix[%lld,%lld] by vector[%zu]",
                    (long long)left_type->as.matrix.rows, (long long)left_type->as.matrix.columns,
                    right_type->as.array.count);
                sem_error_list_add(&ctx->errors, ctx->source_file_path, op_node, buf);
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            if (left_type->as.matrix.element_type != right_type->element_type)
            {
                sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
                    "matrix-vector multiplication element type mismatch");
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            TypeDescriptor const * result_type = get_or_create_array_type(
                ctx->type_registry,
                left_type->as.matrix.element_type,
                (size_t)left_type->as.matrix.rows
            );
            node->resolved_type = (TypeDescriptor *)result_type;
            return result_type;
        }

        // Vector × Matrix (vector-matrix multiplication)
        if (op_md->kind == OP_MUL
            && left_type && left_type->kind == TD_KIND_ARRAY
            && right_type && right_type->kind == TD_KIND_MATRIX)
        {
            if ((int64_t)left_type->as.array.count != right_type->as.matrix.rows)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "vector-matrix multiplication dimension mismatch: cannot multiply vector[%zu] by matrix[%lld,%lld]",
                    left_type->as.array.count,
                    (long long)right_type->as.matrix.rows, (long long)right_type->as.matrix.columns);
                sem_error_list_add(&ctx->errors, ctx->source_file_path, op_node, buf);
                node->resolved_type = (TypeDescriptor *)right_type;
                return right_type;
            }
            if (left_type->element_type != right_type->as.matrix.element_type)
            {
                sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
                    "vector-matrix multiplication element type mismatch");
                node->resolved_type = (TypeDescriptor *)right_type;
                return right_type;
            }
            TypeDescriptor const * result_type = get_or_create_array_type(
                ctx->type_registry,
                right_type->as.matrix.element_type,
                (size_t)right_type->as.matrix.columns
            );
            node->resolved_type = (TypeDescriptor *)result_type;
            return result_type;
        }

        // Matrix * Scalar, Scalar * Matrix (broadcast)
        if (op_md->kind == OP_MUL)
        {
            bool left_scalar = left_type && (is_integer_kind(left_type) || is_floating_kind(left_type));
            bool right_scalar = right_type && (is_integer_kind(right_type) || is_floating_kind(right_type));
            if (left_type && left_type->kind == TD_KIND_MATRIX && right_scalar)
            {
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            if (right_type && right_type->kind == TD_KIND_MATRIX && left_scalar)
            {
                node->resolved_type = (TypeDescriptor *)right_type;
                return right_type;
            }
        }

        // Matrix + Matrix, Matrix - Matrix (element-wise)
        if ((op_md->kind == OP_ADD || op_md->kind == OP_SUB)
            && left_type && left_type->kind == TD_KIND_MATRIX
            && right_type && right_type->kind == TD_KIND_MATRIX)
        {
            if (left_type->as.matrix.rows != right_type->as.matrix.rows
                || left_type->as.matrix.columns != right_type->as.matrix.columns)
            {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "matrix %s dimension mismatch: cannot operate on matrix[%lld,%lld] and matrix[%lld,%lld]",
                    op_md->kind == OP_ADD ? "addition" : "subtraction",
                    (long long)left_type->as.matrix.rows, (long long)left_type->as.matrix.columns,
                    (long long)right_type->as.matrix.rows, (long long)right_type->as.matrix.columns);
                sem_error_list_add(&ctx->errors, ctx->source_file_path, op_node, buf);
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            if (left_type->as.matrix.element_type != right_type->as.matrix.element_type)
            {
                sem_error_list_add(&ctx->errors, ctx->source_file_path, node,
                    "matrix element type mismatch for element-wise operation");
                node->resolved_type = (TypeDescriptor *)left_type;
                return left_type;
            }
            node->resolved_type = (TypeDescriptor *)left_type;
            return left_type;
        }

        // Matrix / Scalar (broadcast)
        if (op_md->kind == OP_DIV && left_type && left_type->kind == TD_KIND_MATRIX
            && right_type && (is_integer_kind(right_type) || is_floating_kind(right_type)))
        {
            node->resolved_type = (TypeDescriptor *)left_type;
            return left_type;
        }
    }

    // Default: propagate left type for scalar arithmetic
    node->resolved_type = (TypeDescriptor *)left_type;
    return left_type;
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
    if (node->list.count < 1 || node->list.children[0] == NULL || node->list.children[0]->text == NULL)
        return NULL;

    char const * field_name = node->list.children[0]->text;

    // Bare .EnumValue: look up the field name as a symbol in scope.
    // Enum values are registered as symbols during enum type resolution.
    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), field_name);
    if (sym && sym->value.type_info)
    {
        node->resolved_type = (TypeDescriptor *)sym->value.type_info;
        return sym->value.type_info;
    }

    sem_error_list_add(&ctx->errors, NULL, node->list.children[0], "undeclared identifier");
    return NULL;
}

static TypeDescriptor const *
sem_evaluate_or_else(SemContext * ctx, odin_grammar_node_t * node)
{

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

// Compute the return type of a matrix intrinsic (transpose, outer_product,
// hadamard_product, matrix_flatten) from its evaluated argument expressions.
// These procs are declared with `---` bodies (see stubs/base/runtime/runtime.odin
// and stubs/base/intrinsics/intrinsics.odin) and are intercepted here (type
// inference) and in ir_gen_postfix.c (codegen) by name. Returns NULL when the
// intrinsic/args don't match.
static TypeDescriptor const *
sem_matrix_intrinsic_result_type(SemContext * ctx, char const * name,
                                 odin_grammar_node_t ** args, int nargs)
{
    if (name == NULL)
        return NULL;

    if (strcmp(name, "transpose") == 0 && nargs == 1
        && args[0] && args[0]->resolved_type)
    {
        TypeDescriptor const * arg_type = args[0]->resolved_type;
        if (arg_type->kind == TD_KIND_MATRIX)
        {
            return get_or_create_matrix_type(ctx->type_registry,
                arg_type->as.matrix.columns, arg_type->as.matrix.rows,
                arg_type->as.matrix.element_type, arg_type->as.matrix.is_row_major);
        }
    }
    else if (strcmp(name, "outer_product") == 0 && nargs == 2
             && args[0] && args[0]->resolved_type
             && args[1] && args[1]->resolved_type)
    {
        TypeDescriptor const * a = args[0]->resolved_type;
        TypeDescriptor const * b = args[1]->resolved_type;
        if (a->kind == TD_KIND_ARRAY && b->kind == TD_KIND_ARRAY
            && a->element_type && b->element_type
            && a->element_type->llvm_type == b->element_type->llvm_type)
        {
            return get_or_create_matrix_type(ctx->type_registry,
                (int64_t)a->as.array.count, (int64_t)b->as.array.count,
                a->element_type, false);
        }
    }
    else if (strcmp(name, "hadamard_product") == 0 && nargs == 2
             && args[0] && args[0]->resolved_type
             && args[1] && args[1]->resolved_type)
    {
        TypeDescriptor const * a = args[0]->resolved_type;
        TypeDescriptor const * b = args[1]->resolved_type;
        if (a->llvm_type && b->llvm_type && a->llvm_type == b->llvm_type
            && (a->kind == TD_KIND_MATRIX || a->kind == TD_KIND_ARRAY))
            return a;
    }
    else if (strcmp(name, "matrix_flatten") == 0 && nargs == 1
             && args[0] && args[0]->resolved_type)
    {
        TypeDescriptor const * arg_type = args[0]->resolved_type;
        if (arg_type->kind == TD_KIND_MATRIX && arg_type->as.matrix.element_type)
        {
            int64_t count = arg_type->as.matrix.rows * arg_type->as.matrix.columns;
            return get_or_create_array_type(ctx->type_registry,
                arg_type->as.matrix.element_type, (size_t)count);
        }
    }
    return NULL;
}

// Evaluate a postfix expression's argument list and collect the individual
// argument nodes (unwrapping the comma chain). Returns the arg count.
static int
sem_collect_call_args_nodes(odin_grammar_node_t * op, odin_grammar_node_t ** out, int max)
{
    odin_grammar_node_t * arg_expr = NULL;
    if (op != NULL && op->list.count > 0 && op->list.children[0] != NULL)
    {
        arg_expr = op->list.children[0];
        if (arg_expr->type == AST_NODE_ARGUMENT_LIST && arg_expr->list.count > 0)
            arg_expr = arg_expr->list.children[0];
    }
    int count = 0;
    if (arg_expr)
        sem_collect_comma_chain_args(arg_expr, out, max, &count);
    return count;
}

static TypeDescriptor const *
sem_evaluate_postfix_expr(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->list.count < 1)
        return NULL;

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
            // Phase 1 import-usage tracking: package-qualified reference marks
            // the import as used. This is called during pass 2 for any
            // expression like `pkg.symbol`. Using imports are tracked
            // separately by `sem_track_using_import_usage` (post-pass).
            if (access_pkg != NULL)
                access_pkg->is_used = true;
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
                    {
                        // Check if the package member is polymorphic; if so,
                        // instantiate a specialization via poly_resolve_call.
                        symbol_t * pkg_callee_sym = NULL;
                        if (i > 0)
                        {
                            odin_grammar_node_t * prev_op = postfix_ops->list.children[i-1];
                            if (prev_op && prev_op->resolved_symbol)
                                pkg_callee_sym = prev_op->resolved_symbol;
                        }
                        else
                        {
                            // First postfix op (i == 0): get callee from base expression
                            odin_grammar_node_t * base = node->list.children[0];
                            if (base != NULL)
                            {
                                odin_grammar_node_t * inner = base;
                                while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
                                    inner = inner->list.children[0];
                                if (inner->type == AST_NODE_POSTFIX_EXPRESSION && inner->list.count >= 2)
                                {
                                    odin_grammar_node_t * inner_ops = inner->list.children[1];
                                    if (inner_ops && inner_ops->list.count > 0)
                                    {
                                        odin_grammar_node_t * last_member = inner_ops->list.children[inner_ops->list.count - 1];
                                        if (last_member && last_member->resolved_symbol)
                                            pkg_callee_sym = last_member->resolved_symbol;
                                    }
                                }
                            }
                        }

                        // Matrix intrinsics (transpose, outer_product,
                        // hadamard_product, matrix_flatten): compute the return
                        // type from the argument types, mirroring the local path.
                        // Handles package-qualified calls like `linalg.hadamard_product`.
                        if (last_member_name
                            && (strcmp(last_member_name, "transpose") == 0
                                || strcmp(last_member_name, "outer_product") == 0
                                || strcmp(last_member_name, "hadamard_product") == 0
                                || strcmp(last_member_name, "matrix_flatten") == 0))
                        {
                            odin_grammar_node_t * mat_args[16];
                            int mat_arg_count = sem_collect_call_args_nodes(op, mat_args, 16);
                            for (int ai = 0; ai < mat_arg_count; ai++)
                            {
                                if (mat_args[ai])
                                    sem_evaluate_expr(ctx, mat_args[ai]);
                            }

                            TypeDescriptor const * result_type
                                = sem_matrix_intrinsic_result_type(ctx, last_member_name, mat_args, mat_arg_count);
                            if (result_type)
                            {
                                op->resolved_symbol = pkg_callee_sym;
                                op->resolved_type = (TypeDescriptor *)result_type;
                                type = result_type;
                                break;
                            }
                        }

                        if (pkg_callee_sym && pkg_callee_sym->is_polymorphic)
                        {
                            odin_grammar_node_t * arg_list = NULL;
                            if (op->list.count > 0 && op->list.children[0] != NULL)
                                arg_list = op->list.children[0];

                            // Evaluate args first (poly_resolve_call reads resolved_type)
                            if (arg_list && arg_list->type == AST_NODE_ARGUMENT_LIST)
                            {
                                for (size_t ai = 0; ai < arg_list->list.count; ai++)
                                {
                                    odin_grammar_node_t * raw = arg_list->list.children[ai];
                                    if (raw == NULL)
                                        continue;
                                    odin_grammar_node_t * chain_args[128];
                                    int chain_count = 0;
                                    sem_collect_comma_chain_args(raw, chain_args, 128, &chain_count);
                                    for (int ci = 0; ci < chain_count; ci++)
                                    {
                                        if (chain_args[ci])
                                            sem_evaluate_expr(ctx, chain_args[ci]);
                                    }
                                }
                            }

                            PolySpecialization * spec = poly_resolve_call(ctx, pkg_callee_sym, op, arg_list);
                            if (spec && spec->symbol)
                            {
                                op->resolved_symbol = spec->symbol;
                                TypeDescriptor const * proc_type = spec->symbol->value.type_info;
                                if (proc_type && proc_type->kind == TD_KIND_PROC)
                                {
                                    if (proc_type->proc_metadata.return_count > 1)
                                    {
                                        op->resolved_type = (TypeDescriptor *)proc_type;
                                        type = proc_type;
                                    }
                                    else
                                    {
                                        type = proc_type->proc_metadata.return_type;
                                        op->resolved_type = (TypeDescriptor *)type;
                                    }
                                }
                            }
                            else
                            {
                                sem_error_list_add(&ctx->errors, NULL, op,
                                                   "polymorphic procedure call could not be specialized");
                            }
                            break;
                        }

                        // Propagate resolved_symbol to the CALL node so IR gen
                        // can create forward declarations for cross-package calls.
                        if (pkg_callee_sym)
                            op->resolved_symbol = pkg_callee_sym;

                        if (type && type->kind == TD_KIND_PROC)
                        {
                            if (op->list.count > 0 && op->list.children[0] != NULL)
                            {
                                odin_grammar_node_t * arg_list = op->list.children[0];
                                if (arg_list->type == AST_NODE_ARGUMENT_LIST)
                    {
                        for (size_t ai = 0; ai < arg_list->list.count; ai++)
                        {
                            if (arg_list->list.children[ai])
                            {
                                odin_grammar_node_t * argn = arg_list->list.children[ai];
                                sem_evaluate_expr(ctx, argn);
                            }
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
                    }

                    case AST_NODE_POSTFIX_SUBSCRIPT:
                    {
                        odin_grammar_node_t * index_node = op->list.children[0];
                        int index_count = 1;
                        
                        if (index_node && index_node->type == AST_NODE_EXPRESSION && index_node->list.count >= 2)
                        {
                            index_count = (int)index_node->list.count;
                        }
                        
                        for (int idx = 0; idx < index_count; idx++)
                        {
                            odin_grammar_node_t * single_index_node = index_node;
                            if (index_count > 1 && index_node->type == AST_NODE_EXPRESSION)
                            {
                                single_index_node = index_node->list.children[idx];
                            }
                            
                            if (type
                                && (type->kind == TD_KIND_ARRAY || type->kind == TD_KIND_SLICE
                                    || type->kind == TD_KIND_MULTI_POINTER || type->kind == TD_KIND_VECTOR))
                            {
                                type = type->element_type;
                            }
                            else if (type && type->kind == TD_KIND_MATRIX)
                            {
                                // Matrix indexing requires both row and column indices: m[row, col]
                                if (index_count == 1)
                                {
                                    sem_error_list_add(&ctx->errors, ctx->source_file_path, op,
                                        "matrix index requires both a row and column index: use m[row, col]");
                                    type = NULL;
                                    break;
                                }
                                type = type->as.matrix.element_type;
                            }
                            else if (type && type->kind == TD_KIND_MAP)
                            {
                                type = type->as.map.value_type;
                            }
                            else if (type && type->kind == TD_KIND_BASIC && type->as.basic.name != NULL
                                     && strcmp(type->as.basic.name, "string") == 0)
                            {
                                type = get_basic_type_by_name(ctx->type_registry, "u8");
                            }
                        }
                        
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
        {
            // Check if the callee is a polymorphic procedure.
            // Walk the base expression to find the resolved symbol.
            symbol_t * callee_sym = NULL;
            odin_grammar_node_t * base = node->list.children[0];
            odin_grammar_node_t * inner = NULL;
            if (base != NULL)
            {
                inner = base;
                while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
                    inner = inner->list.children[0];
                if (inner->type == AST_NODE_IDENTIFIER)
                    callee_sym = inner->resolved_symbol;
            }

            // Handle matrix intrinsics specially (transpose, outer_product,
            // hadamard_product, matrix_flatten) - compute the return type from
            // the argument types. These are declared with `---` bodies and are
            // intercepted here + in ir_gen_postfix_call by name.
            if (callee_sym && inner && inner->text
                && (strcmp(inner->text, "transpose") == 0
                    || strcmp(inner->text, "outer_product") == 0
                    || strcmp(inner->text, "hadamard_product") == 0
                    || strcmp(inner->text, "matrix_flatten") == 0))
            {
                odin_grammar_node_t * mat_args[16];
                int mat_arg_count = sem_collect_call_args_nodes(op, mat_args, 16);
                for (int ai = 0; ai < mat_arg_count; ai++)
                {
                    if (mat_args[ai])
                        sem_evaluate_expr(ctx, mat_args[ai]);
                }

                TypeDescriptor const * result_type
                    = sem_matrix_intrinsic_result_type(ctx, inner->text, mat_args, mat_arg_count);
                if (result_type)
                {
                    op->resolved_symbol = callee_sym;
                    op->resolved_type = (TypeDescriptor *)result_type;
                    type = result_type;  // Update type for POSTFIX_EXPRESSION
                    node->resolved_type = (TypeDescriptor *)type;  // Also set on the node
                    break;
                }
            }

            if (callee_sym && callee_sym->is_polymorphic)
            {
                // Evaluate argument expressions first
                odin_grammar_node_t * arg_list = NULL;
                if (op->list.count > 0 && op->list.children[0] != NULL)
                {
                    arg_list = op->list.children[0];
                    if (arg_list->type == AST_NODE_ARGUMENT_LIST)
                    {
                        for (size_t ai = 0; ai < arg_list->list.count; ai++)
                        {
                            odin_grammar_node_t * raw = arg_list->list.children[ai];
                            if (raw == NULL)
                                continue;
                            odin_grammar_node_t * chain_args[128];
                            int chain_count = 0;
                            sem_collect_comma_chain_args(raw, chain_args, 128, &chain_count);
                            for (int ci = 0; ci < chain_count; ci++)
                            {
                                if (chain_args[ci])
                                    sem_evaluate_expr(ctx, chain_args[ci]);
                            }
                        }
                    }
                }

                PolySpecialization * spec = poly_resolve_call(ctx, callee_sym, op, arg_list);
                if (spec && spec->symbol)
                {
                    op->resolved_symbol = spec->symbol;
                    TypeDescriptor const * proc_type = spec->symbol->value.type_info;
                    if (proc_type && proc_type->kind == TD_KIND_PROC)
                    {
                        if (proc_type->proc_metadata.return_count > 1)
                        {
                            op->resolved_type = (TypeDescriptor *)proc_type;
                            type = proc_type;
                        }
                        else
                        {
                            type = proc_type->proc_metadata.return_type;
                            op->resolved_type = (TypeDescriptor *)type;
                        }
                    }
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op,
                                       "polymorphic procedure call could not be specialized");
                }
                break;
            }

            if (type && type->kind == TD_KIND_PROC)
            {
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
        }

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
            else if (type && type->kind == TD_KIND_ENUM && op->list.count >= 1 && op->list.children[0])
            {
                char const * field_name = op->list.children[0]->text;
                if (field_name == NULL)
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "enum member access: missing field name");
                    break;
                }
                bool found = false;
                for (int ei = 0; ei < type->as.enum_type.enumerator_count; ei++)
                {
                    if (type->as.enum_type.enumerator_names[ei] != NULL
                        && strcmp(field_name, type->as.enum_type.enumerator_names[ei]) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    op->resolved_type = (TypeDescriptor *)type;
                }
                else
                {
                    sem_error_list_add(&ctx->errors, NULL, op, "enum type has no member named");
                    type = NULL;
                }
            }
            break;

        case AST_NODE_POSTFIX_SUBSCRIPT:
            {
                odin_grammar_node_t * index_node = op->list.children[0];
                int index_count = 1;
                
                // Check if this is a multi-index subscript (comma-separated indices)
                // e.g., m[0, 1] should be treated as m[0][1]
                if (index_node && index_node->type == AST_NODE_EXPRESSION && index_node->list.count >= 2)
                {
                    index_count = (int)index_node->list.count;
                }
                
                for (int idx = 0; idx < index_count; idx++)
                {
                    odin_grammar_node_t * single_index_node = index_node;
                    if (index_count > 1 && index_node->type == AST_NODE_EXPRESSION)
                    {
                        single_index_node = index_node->list.children[idx];
                    }
                    
                    if (type
                        && (type->kind == TD_KIND_ARRAY || type->kind == TD_KIND_SLICE
                            || type->kind == TD_KIND_MULTI_POINTER || type->kind == TD_KIND_VECTOR))
                    {
                        type = type->element_type;
                    }
                    else if (type && type->kind == TD_KIND_MATRIX)
                    {
                        // Matrix indexing requires both row and column indices: m[row, col]
                        if (index_count == 1)
                        {
                            sem_error_list_add(&ctx->errors, ctx->source_file_path, op,
                                "matrix index requires both a row and column index: use m[row, col]");
                            type = NULL;
                            break;
                        }
                        // First index selects the element position (row in math terms);
                        // the multi-index subscript consumes both indices → element type
                        type = type->as.matrix.element_type;
                    }
                    else if (type && type->kind == TD_KIND_MAP)
                    {
                        type = type->as.map.value_type;
                    }
                    else if (type && type->kind == TD_KIND_BASIC && type->as.basic.name != NULL
                             && strcmp(type->as.basic.name, "string") == 0)
                    {
                        type = get_basic_type_by_name(ctx->type_registry, "u8");
                    }
                }
                
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
                op->resolved_type = (TypeDescriptor *)type;
            }
            else if (type && type->kind == TD_KIND_ARRAY)
            {
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

// --- Overload bundle call resolution ---

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

    // Evaluate argument expressions and collect their types. The argument list
    // wraps a single comma-chained Expression tree, so decompose it (the direct
    // children loop would count a multi-arg call as a single argument).
    int arg_count = 0;
    TypeDescriptor const * arg_types[128];
    if (arg_list_node != NULL && arg_list_node->type == AST_NODE_ARGUMENT_LIST && arg_list_node->list.count >= 1)
    {
        odin_grammar_node_t * arg_expr = arg_list_node->list.children[0];
        odin_grammar_node_t * args[128];
        sem_collect_comma_chain_args(arg_expr, args, 128, &arg_count);
        for (int ai = 0; ai < arg_count && ai < 128; ai++)
        {
            sem_evaluate_expr(ctx, args[ai]);
            arg_types[ai] = args[ai]->resolved_type;
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

    if (match_count > 1)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "ambiguous call to '%s' — %d overloads match", callee_name ? callee_name : "?", match_count);
        sem_error_list_add(&ctx->errors, NULL, call_op, buf);
        return NULL;
    }

    // No non-poly match found — try polymorphic candidates
    if (match_count == 0 && candidate_count > 0)
    {
        int poly_match_count = 0;
        symbol_t * poly_winner = NULL;

        for (int ci = 0; ci < candidate_count; ci++)
        {
            symbol_t * cand_sym = candidate_symbols[ci];
            if (cand_sym == NULL || !cand_sym->is_polymorphic)
                continue;

            // Try to resolve the polymorphic call. Save/restore error list
            // to suppress spurious errors from non-matching candidates.
            int saved_error_count = ctx->errors.count;
            PolySpecialization * spec = poly_resolve_call(ctx, cand_sym, call_op, arg_list_node);
            if (spec && spec->symbol)
            {
                poly_winner = spec->symbol;
                poly_match_count++;
            }
            else
            {
                // Restore error state — this candidate didn't match
                ctx->errors.count = saved_error_count;
            }
        }

        if (poly_match_count > 1)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "ambiguous call to '%s' — %d polymorphic overloads match", callee_name ? callee_name : "?", poly_match_count);
            sem_error_list_add(&ctx->errors, NULL, call_op, buf);
            return NULL;
        }

        if (poly_match_count == 1 && poly_winner)
        {
            return poly_winner;
        }

        // No match at all — fall through to "no matching overload" error
        char buf[256];
        snprintf(buf, sizeof(buf), "no matching overload for '%s' with %d argument(s)", callee_name ? callee_name : "?", arg_count);
        sem_error_list_add(&ctx->errors, NULL, call_op, buf);
        return NULL;
    }

    if (match_count == 1)
    {
        return best_match;
    }

    // match_count == 0 but no poly candidates were tried (candidate_count == 0)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "no matching overload for '%s' with %d argument(s)", callee_name ? callee_name : "?", arg_count);
        sem_error_list_add(&ctx->errors, NULL, call_op, buf);
        return NULL;
    }
}
