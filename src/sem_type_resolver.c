#include "sem_type_resolver.h"

#include "ast_utils.h"
#include "polymorphism.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"
#include "semantic_analyser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Type expression resolution ---

static TypeDescriptor const *
sem_resolve_type_name(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->text == NULL)
        return NULL;
    return get_basic_type_by_name(ctx->type_registry, node->text);
}


// --- Dispatch table for sem_resolve_type_expr ---

static TypeDescriptor const * sem_resolve_array_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_basic_or_type_name(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_bit_field_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_bit_set_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_distinct_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_dynamic_array_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_enum_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_map_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_maybe_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_multi_pointer_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_pointer_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_proc_sig_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_slice_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_soa_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_struct_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_tuple_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_poly_ident_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_type_identifier(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_type_application(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_union_type(SemContext * ctx, odin_grammar_node_t * node);
static TypeDescriptor const * sem_resolve_vector_type(SemContext * ctx, odin_grammar_node_t * node);

static TypeDescriptor const * (* const sem_resolve_type_dispatch[])(SemContext *, odin_grammar_node_t *) = {
    [AST_NODE_ARRAY_TYPE] = sem_resolve_array_type,
    [AST_NODE_BASIC_TYPE] = sem_resolve_basic_or_type_name,
    [AST_NODE_BIT_FIELD_TYPE] = sem_resolve_bit_field_type,
    [AST_NODE_BIT_SET_TYPE] = sem_resolve_bit_set_type,
    [AST_NODE_DISTINCT_TYPE] = sem_resolve_distinct_type,
    [AST_NODE_DYNAMIC_ARRAY_TYPE] = sem_resolve_dynamic_array_type,
    [AST_NODE_ENUM_TYPE] = sem_resolve_enum_type,
    [AST_NODE_IDENTIFIER] = sem_resolve_type_identifier,
    [AST_NODE_MAP_TYPE] = sem_resolve_map_type,
    [AST_NODE_MAYBE_TYPE] = sem_resolve_maybe_type,
    [AST_NODE_MULTI_POINTER_TYPE] = sem_resolve_multi_pointer_type,
    [AST_NODE_POINTER_TYPE] = sem_resolve_pointer_type,
    [AST_NODE_POLY_IDENT] = sem_resolve_poly_ident_type,
    [AST_NODE_PROCEDURE_SIGNATURE] = sem_resolve_proc_sig_type,
    [AST_NODE_SLICE_TYPE] = sem_resolve_slice_type,
    [AST_NODE_SOA_TYPE] = sem_resolve_soa_type,
    [AST_NODE_STRUCT_TYPE] = sem_resolve_struct_type,
    [AST_NODE_TUPLE_TYPE] = sem_resolve_tuple_type,
    [AST_NODE_TYPE_NAME] = sem_resolve_basic_or_type_name,
    [AST_NODE_TYPE_APPLICATION] = sem_resolve_type_application,
    [AST_NODE_UNION_TYPE] = sem_resolve_union_type,
    [AST_NODE_VECTOR_TYPE] = sem_resolve_vector_type,
};

TypeDescriptor const *
sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;
    if ((size_t)node->type < AST_NODE_COUNT && sem_resolve_type_dispatch[node->type])
        return sem_resolve_type_dispatch[node->type](ctx, node);
    return NULL;
}

// --- Extracted case functions ---

static TypeDescriptor const *
sem_resolve_proc_sig_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_basic_or_type_name(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_pointer_type(SemContext * ctx, odin_grammar_node_t * node)
{

    TypeDescriptor const * pointee = sem_resolve_type_expr(ctx, node->list.children[0]);
    if (pointee == NULL)
        return NULL;
    return get_or_create_pointer_type(ctx->type_registry, pointee);
    
}

static TypeDescriptor const *
sem_resolve_array_type(SemContext * ctx, odin_grammar_node_t * node)
{

    // ArrayType = LBracket (IntegerLiteral | PolyIdent | Identifier)? RBracket TypePrefix
    // Children: for [$N]$T: [PolyIdent("$N"), PolyIdent("$T")]
    //          for [3]int: [IntegerValue("3"), BasicType("int")]

    // Step 1: Determine the array size (count).
    odin_grammar_node_t * size_node = NULL;
    size_t count = 0;

    // Try IntegerLiteral first
    size_node = node_find_child(node, AST_NODE_INTEGER_VALUE);
    if (size_node && size_node->text)
    {
        count = (size_t)parse_odin_unsigned(size_node->text, NULL, 0);
    }
    else
    {
        // Try PolyIdent for $N during poly instantiation — must check before
        // element type search because is_type_node(POLY_IDENT) is true and
        // would misidentify $N as the element type.
        odin_grammar_node_t * poly_node = node_find_child(node, AST_NODE_POLY_IDENT);
        if (poly_node && poly_node->text)
        {
            char const * poly_name = poly_node->text;
            if (poly_name[0] == '$')
                poly_name++;
            long long val = 0;
            if (poly_env_lookup_int(ctx, poly_name, &val))
            {
                count = (size_t)val;
                size_node = poly_node; // mark as consumed for size
            }
        }
        if (count == 0)
        {
            // Try Identifier for bare T (declared by preceding $T: typeid param)
            odin_grammar_node_t * id_node = node_find_child(node, AST_NODE_IDENTIFIER);
            if (id_node && id_node->text)
            {
                long long val = 0;
                if (poly_env_lookup_int(ctx, id_node->text, &val))
                {
                    count = (size_t)val;
                    size_node = id_node;
                }
            }
        }
    }

    // Step 2: Find the element type node — skip POLY_IDENT if it was already
    // consumed as the size (e.g. $N in [$N]$T).
    odin_grammar_node_t * elem_type_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        // Skip the POLY_IDENT that was used as the size
        if (child == size_node)
            continue;
        if (child->type == AST_NODE_IDENTIFIER || is_type_node(child))
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

    TypeDescriptor const * arr_type = get_or_create_array_type(ctx->type_registry, elem_type, count);
    if (arr_type)
        node->resolved_type = (TypeDescriptor *)arr_type;
    return arr_type;
    
}

static TypeDescriptor const *
sem_resolve_distinct_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_slice_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_multi_pointer_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_dynamic_array_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_map_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_bit_field_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_bit_set_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_enum_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_struct_type(SemContext * ctx, odin_grammar_node_t * node)
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
            else if (child->type == AST_NODE_IDENTIFIER && name_node != NULL)
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

static TypeDescriptor const *
sem_resolve_union_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_soa_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_maybe_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_tuple_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_vector_type(SemContext * ctx, odin_grammar_node_t * node)
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

static TypeDescriptor const *
sem_resolve_poly_ident_type(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return NULL;
    // Strip leading $ from the poly ident name (the lexeme captures "$T" but
    // the poly env stores entries with the name "T").
    char const * lookup_name = node->text;
    if (lookup_name[0] == '$')
        lookup_name++;
    TypeDescriptor const * td = poly_env_lookup_type(ctx, lookup_name);
    if (td)
    {
        node->resolved_type = (TypeDescriptor *)td;
        return td;
    }
    return NULL;
}

static TypeDescriptor const *
sem_resolve_type_identifier(SemContext * ctx, odin_grammar_node_t * node)
{

    if (node->text == NULL)
        return NULL;

    // When a polymorphic instantiation is active, check the poly env stack
    // first. Poly variable names like `T` appear as bare Identifier nodes
    // in return types and body expressions.
    if (ctx->poly_env_stack_depth > 0)
    {
        TypeDescriptor const * poly_type = poly_env_lookup_type(ctx, node->text);
        if (poly_type)
        {
            node->resolved_type = (TypeDescriptor *)poly_type;
            return poly_type;
        }
    }

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

// --- TypeApplication: Box(int), Vec2(f64), etc. ---

// Collect every AST_NODE_PARAMETER found under a PARAMETER_LIST node.
// The AST nesting is: PARAMETER_LIST -> PARAMETERS -> PARAMETER* (optionally
// trailing ELLIPSIS).  Returns the count and fills `out` (capacity
// `max_params`).
static int
collect_parameters_from_param_list(odin_grammar_node_t * param_list,
                                   odin_grammar_node_t ** out, int max_params)
{
    int count = 0;
    for (size_t i = 0; i < param_list->list.count && count < max_params; i++)
    {
        odin_grammar_node_t * child = param_list->list.children[i];
        if (child == NULL || child->type != AST_NODE_PARAMETERS)
            continue;
        for (size_t j = 0; j < child->list.count && count < max_params; j++)
        {
            odin_grammar_node_t * param = child->list.children[j];
            if (param != NULL && param->type == AST_NODE_PARAMETER)
                out[count++] = param;
        }
    }
    return count;
}

static TypeDescriptor const *
sem_resolve_type_application(SemContext * ctx, odin_grammar_node_t * node)
{
    // TypeApplication children: [Identifier("Box"), TypeArg1, TypeArg2, ...]
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER || name_node->text == NULL)
        return NULL;

    // Look up the identifier — must be a polymorphic type
    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), name_node->text);
    if (sym == NULL || !sym->is_polymorphic || sym->kind != SYMBOL_TYPE)
        return NULL;

    // Get the origin ConstantDecl AST
    odin_grammar_node_t * origin = poly_get_origin(sym);
    if (origin == NULL)
        return NULL;

    // Extract the StructType from the ConstantDecl
    // ConstantDecl children: [Identifier("Box"), StructType]
    odin_grammar_node_t * struct_type = NULL;
    for (size_t i = 0; i < origin->list.count; i++)
    {
        odin_grammar_node_t * child = origin->list.children[i];
        if (child != NULL && child != name_node && child->type == AST_NODE_STRUCT_TYPE)
        {
            struct_type = child;
            break;
        }
    }
    if (struct_type == NULL)
        return NULL;

    // Extract the ParameterList from the StructType
    odin_grammar_node_t * param_list = node_find_child(struct_type, AST_NODE_PARAMETER_LIST);
    if (param_list == NULL)
        return NULL;

    // Collect individual PARAMETER nodes (unwrap PARAMETERS wrapper).
    odin_grammar_node_t * params[32];
    int param_count = collect_parameters_from_param_list(param_list, params, 32);

    // Count type arguments (all children except the name)
    int arg_count = (int)node->list.count - 1;
    if (arg_count <= 0)
        return NULL;
    if (param_count == 0)
        return NULL;
    if (arg_count != param_count)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "type '%s' expects %d type arguments but got %d",
                 name_node->text, param_count, arg_count);
        sem_error_list_add(&ctx->errors, ctx->source_file_path, node, buf);
        return NULL;
    }

    // Build poly env by matching params against type arguments
    PolyEnv env = {0};
    for (int arg_idx = 0; arg_idx < param_count; arg_idx++)
    {
        odin_grammar_node_t * param = params[arg_idx];

        // Extract param name (first Identifier or PolyIdent child)
        odin_grammar_node_t * param_name = NULL;
        odin_grammar_node_t * param_type_node = NULL;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_POLY_IDENT || child->type == AST_NODE_IDENTIFIER)
            {
                if (param_name == NULL)
                {
                    param_name = child;
                    continue;
                }
            }
            if (is_type_node(child) || (child->type == AST_NODE_IDENTIFIER && param_name != NULL))
            {
                param_type_node = child;
                break;
            }
        }
        if (param_name == NULL || param_name->text == NULL)
            continue;
        if (param_type_node == NULL)
            continue;

        // Determine if this param is a type param ($T: typeid) or int param ($N: int)
        // by checking the param's declared type text.
        char const * ptype_text = param_type_node->text;
        bool is_int_param = (ptype_text != NULL
                             && (strcmp(ptype_text, "int") == 0
                                 || strcmp(ptype_text, "i8") == 0
                                 || strcmp(ptype_text, "i16") == 0
                                 || strcmp(ptype_text, "i32") == 0
                                 || strcmp(ptype_text, "i64") == 0
                                 || strcmp(ptype_text, "i128") == 0
                                 || strcmp(ptype_text, "u8") == 0
                                 || strcmp(ptype_text, "u16") == 0
                                 || strcmp(ptype_text, "u32") == 0
                                 || strcmp(ptype_text, "u64") == 0
                                 || strcmp(ptype_text, "u128") == 0));

        odin_grammar_node_t * type_arg = node->list.children[1 + arg_idx];
        if (type_arg == NULL)
            continue;

        // Strip $ from param name
        char const * pname = param_name->text;
        if (pname[0] == '$')
            pname++;

        if (is_int_param)
        {
            // Evaluate the argument as a compile-time integer constant
            int const_ok = 0;
            long long int_val = sem_evaluate_constant_int(ctx, type_arg, &const_ok);
            if (!const_ok)
                return NULL;
            env.entries[env.count].name = strdup(pname);
            env.entries[env.count].kind = POLY_SLOT_INT;
            env.entries[env.count].bound_int_value = int_val;
            env.count++;
        }
        else
        {
            // Type param — resolve the argument as a type
            TypeDescriptor const * arg_type = sem_resolve_type_expr(ctx, type_arg);
            if (arg_type == NULL)
                return NULL;
            env.entries[env.count].name = strdup(pname);
            env.entries[env.count].kind = POLY_SLOT_TYPE;
            env.entries[env.count].bound_type = arg_type;
            env.count++;
        }
    }

    // Push env and resolve the struct type
    poly_env_push(ctx, &env);
    TypeDescriptor const * result = sem_resolve_struct_type(ctx, struct_type);
    poly_env_pop(ctx);

    if (result != NULL)
        node->resolved_type = (TypeDescriptor *)result;
    return result;
}
