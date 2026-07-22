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
    [AST_NODE_EXPAND_VALUES_EXPR] = sem_evaluate_expand_values_expr,
    [AST_NODE_COMPRESS_VALUES_EXPR] = sem_evaluate_compress_values_expr,
    [AST_NODE_SOA_ZIP_EXPR] = sem_evaluate_soa_zip_expr,
    [AST_NODE_SOA_UNZIP_EXPR] = sem_evaluate_soa_unzip_expr,
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
    {
            TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
            TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
            (void)right_type;
            node->resolved_type = (TypeDescriptor *)left_type;
            return left_type;
        }

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

    if (node->list.count >= 1 && node->list.children[0] && node->list.children[0]->text)
    {
        char const * field_name = node->list.children[0]->text;
        (void)field_name;
    }
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
        {
            // Check if the callee is a polymorphic procedure.
            // Walk the base expression to find the resolved symbol.
            symbol_t * callee_sym = NULL;
            odin_grammar_node_t * base = node->list.children[0];
            if (base != NULL)
            {
                odin_grammar_node_t * inner = base;
                while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
                    inner = inner->list.children[0];
                if (inner->type == AST_NODE_IDENTIFIER)
                    callee_sym = inner->resolved_symbol;
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
