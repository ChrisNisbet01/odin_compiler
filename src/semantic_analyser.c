#include "semantic_analyser.h"

#include "ast_utils.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
sem_context_init(
    SemContext * ctx, odin_grammar_node_t * ast, TypeDescriptors * type_registry, GeneratorContext * gen_ctx
)
{
    ctx->ast = ast;
    ctx->type_registry = type_registry;
    ctx->gen_ctx = gen_ctx;
    sem_error_list_init(&ctx->errors);
}

// --- Forward declarations ---
static TypeDescriptor const * sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node);
static void sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type);

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
        return sem_resolve_type_name(ctx, node);

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
            if (is_type_node(node->list.children[i]))
            {
                elem_type_node = node->list.children[i];
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
            count = (size_t)strtoull(size_node->text, NULL, 0);
        }

        TypeDescriptor const * arr_type = get_or_create_array_type(ctx->type_registry, elem_type, count);
        if (arr_type)
            node->resolved_type = (TypeDescriptor *)arr_type;
        return arr_type;
    }

    case AST_NODE_DISTINCT_TYPE:
    {
        // DistinctType = KwDistinct TypePrefix
        // Resolve the inner type and return its descriptor (treating distinct as transparent)
        odin_grammar_node_t * inner = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (is_type_node(node->list.children[i]))
            {
                inner = node->list.children[i];
                break;
            }
        }
        if (inner == NULL)
            return NULL;
        TypeDescriptor const * base = sem_resolve_type_expr(ctx, inner);
        if (base)
            node->resolved_type = (TypeDescriptor *)base;
        return base;
    }

    case AST_NODE_SLICE_TYPE:
    {
        // SliceType = LBracket RBracket TypePrefix
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (is_type_node(node->list.children[i]))
            {
                elem_type_node = node->list.children[i];
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

    case AST_NODE_DYNAMIC_ARRAY_TYPE:
    {
        odin_grammar_node_t * elem_type_node = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (is_type_node(node->list.children[i]))
            {
                elem_type_node = node->list.children[i];
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

    case AST_NODE_PROCEDURE_SIGNATURE:
    {
        TypeDescriptor const * return_type = NULL;
        int param_count = 0;
        TypeDescriptor const ** params = NULL;
        size_t param_cap = 0;
        bool is_variadic = false;

        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_RETURNS && child->list.count > 0)
            {
                return_type = sem_resolve_type_expr(ctx, child->list.children[0]);
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

                        // Parameter = KwUsing? (PolyIdent Colon)? Identifier Colon TypePrefix
                        // Find the TypePrefix child (last non-NULL child)
                        odin_grammar_node_t * type_node = NULL;
                        for (size_t ci = 0; ci < param->list.count; ci++)
                        {
                            odin_grammar_node_t * pc = param->list.children[ci];
                            if (pc != NULL)
                                type_node = pc;
                        }

                        if (type_node == NULL)
                            continue;

                        TypeDescriptor const * pt = sem_resolve_type_expr(ctx, type_node);
                        if (pt == NULL)
                            continue;

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

        TypeDescriptor const * proc_type
            = get_or_create_proc_type(ctx->type_registry, return_type, params, param_count, NULL, 0, is_variadic);
        free((void *)params);
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

        if (enumerator_list)
        {
            int next_value = 0;
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

                next_value++;
            }
        }

        if (enum_td)
            node->resolved_type = (TypeDescriptor *)enum_td;
        return enum_td;
    }

    case AST_NODE_STRUCT_TYPE:
    {
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
        if (field_names == NULL || field_types == NULL || llvm_field_types == NULL || field_is_using == NULL)
        {
            free((void *)field_names);
            free((void *)field_types);
            free(llvm_field_types);
            free(field_is_using);
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

            // Find field name (first Identifier child)
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

            field_names[fi] = name_node->text;
            field_types[fi] = ftype;
            llvm_field_types[fi] = ftype->llvm_type;
            field_is_using[fi] = is_using;
            fi++;
        }

        if (fi == 0)
        {
            free((void *)field_names);
            free((void *)field_types);
            free(llvm_field_types);
            free(field_is_using);
            return NULL;
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
            members.fields[j].offset = 0;
            members.fields[j].bit_offset = 0;
            members.fields[j].bit_width = 0;
            members.fields[j].storage_index = 0;
        }

        TypeDescriptor const * struct_td = register_struct_type(ctx->type_registry, llvm_struct, true, &members);

        free((void *)field_names);
        free((void *)field_types);
        free(llvm_field_types);
        free(field_is_using);
        free(members.fields);

        if (struct_td)
            node->resolved_type = (TypeDescriptor *)struct_td;
        return struct_td;
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
    {
        TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
        if (int_type)
        {
            node->resolved_type = (TypeDescriptor *)int_type;
        }
        return int_type;
    }

    case AST_NODE_FLOAT_VALUE:
    {
        TypeDescriptor const * f64_type = get_basic_type_by_name(ctx->type_registry, "f64");
        if (f64_type)
        {
            node->resolved_type = (TypeDescriptor *)f64_type;
        }
        return f64_type;
    }

    case AST_NODE_STRING_LITERAL:
    case AST_NODE_RAW_STRING_LITERAL:
    {
        TypeDescriptor const * str_type = get_basic_type_by_name(ctx->type_registry, "string");
        if (str_type)
        {
            node->resolved_type = (TypeDescriptor *)str_type;
        }
        return str_type;
    }

    case AST_NODE_RUNE_LITERAL:
    {
        TypeDescriptor const * rune_type = get_basic_type_by_name(ctx->type_registry, "rune");
        if (rune_type)
        {
            node->resolved_type = (TypeDescriptor *)rune_type;
        }
        return rune_type;
    }

    case AST_NODE_BOOL_TRUE:
    case AST_NODE_BOOL_FALSE:
    {
        TypeDescriptor const * bool_type = get_basic_type_by_name(ctx->type_registry, "bool");
        if (bool_type)
        {
            node->resolved_type = (TypeDescriptor *)bool_type;
        }
        return bool_type;
    }

    case AST_NODE_CAST_EXPR:
    case AST_NODE_TRANSMUTE_EXPR:
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

    case AST_NODE_LEN_EXPR:
    case AST_NODE_CAP_EXPR:
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
                     || (operand_type->kind == TD_KIND_DYNAMIC_ARRAY)
                     || (operand_type->kind == TD_KIND_BASIC && operand_type->as.basic.name != NULL
                         && strcmp(operand_type->as.basic.name, "string") == 0 && node->type == AST_NODE_LEN_EXPR);
        if (!valid)
        {
            sem_error_list_add(
                &ctx->errors,
                node,
                node->type == AST_NODE_LEN_EXPR ? "invalid operand type for len" : "invalid operand type for cap"
            );
            return NULL;
        }

        TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
        node->resolved_type = (TypeDescriptor *)int_type;
        return int_type;
    }

    case AST_NODE_MAKE_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        odin_grammar_node_t * type_node = node->list.children[0];
        odin_grammar_node_t * len_node = node->list.children[1];
        TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
        if (td == NULL)
        {
            sem_error_list_add(&ctx->errors, node, "invalid type argument to make");
            return NULL;
        }
        if (td->kind != TD_KIND_SLICE && td->kind != TD_KIND_DYNAMIC_ARRAY)
        {
            sem_error_list_add(&ctx->errors, node, "make only supports slice and dynamic array types");
            return NULL;
        }
        sem_evaluate_expr(ctx, len_node);
        node->resolved_type = (TypeDescriptor *)td;
        return td;
    }

    case AST_NODE_NEW_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * type_node = node->list.children[0];
        TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
        if (td == NULL)
        {
            sem_error_list_add(&ctx->errors, node, "invalid type argument to new");
            return NULL;
        }
        TypeDescriptor const * ptr_type = get_or_create_pointer_type(ctx->type_registry, td);
        node->resolved_type = (TypeDescriptor *)ptr_type;
        return ptr_type;
    }

    case AST_NODE_DELETE_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        sem_evaluate_expr(ctx, node->list.children[0]);
        node->resolved_type = NULL;
        return NULL;
    }

    case AST_NODE_DISTINCT_TYPE:
    {
        TypeDescriptor const * td = sem_resolve_type_expr(ctx, node);
        node->resolved_type = (TypeDescriptor *)td;
        return td;
    }

    case AST_NODE_NIL:
    {
        node->resolved_type = NULL;
        return NULL;
    }

    case AST_NODE_IDENTIFIER:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
        if (sym)
        {
            node->resolved_symbol = sym;
            node->resolved_type = (TypeDescriptor *)sym->value.type_info;
            return sym->value.type_info;
        }
        sem_error_list_add(&ctx->errors, node, "undeclared identifier");
        return NULL;
    }

    case AST_NODE_UNARY_EXPRESSION:
    {
        TypeDescriptor const * operand_type = sem_evaluate_expr(ctx, node->list.children[1]);
        node->resolved_type = (TypeDescriptor *)operand_type;
        return operand_type;
    }

    case AST_NODE_RANGE_EXPRESSION:
    {
        TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
        TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
        if (left_type == NULL || right_type == NULL)
            return NULL;
        if (!is_integer_kind(left_type) || !is_integer_kind(right_type))
        {
            sem_error_list_add(&ctx->errors, node, "Range expression requires integer operands");
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

    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    {
        TypeDescriptor const * left_type = sem_evaluate_expr(ctx, node->list.children[0]);
        TypeDescriptor const * right_type = sem_evaluate_expr(ctx, node->list.children[2]);
        (void)right_type;
        node->resolved_type = (TypeDescriptor *)left_type;
        return left_type;
    }

    case AST_NODE_POSTFIX_CALL:
    {
        TypeDescriptor const * callee_type = sem_evaluate_expr(ctx, node->list.children[0]);
        if (callee_type && callee_type->kind == TD_KIND_PROC)
        {
            TypeDescriptor const * ret_type = callee_type->proc_metadata.return_type;
            node->resolved_type = (TypeDescriptor *)ret_type;
            return ret_type;
        }
        return NULL;
    }

    case AST_NODE_POSTFIX_MEMBER:
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

    case AST_NODE_OR_ELSE:
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
        TypeDescriptor const * result_type = lhs_type ? lhs_type : rhs_type;
        if (result_type)
            node->resolved_type = (TypeDescriptor *)result_type;
        return result_type;
    }

    case AST_NODE_OR_RETURN:
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

    case AST_NODE_TERNARY_EXPRESSION:
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

    case AST_NODE_EXPRESSION:
    case AST_NODE_ASSIGN_EXPRESSION:
    case AST_NODE_PRIMARY_EXPRESSION:
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

    case AST_NODE_POSTFIX_EXPRESSION:
    {
        if (node->list.count < 1)
            return NULL;

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
                    type = type->proc_metadata.return_type;
                    op->resolved_type = (TypeDescriptor *)type;
                }
                break;

            case AST_NODE_POSTFIX_MEMBER:
                if (type && type->kind == TD_KIND_STRUCT && op->list.count >= 1 && op->list.children[0])
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
                break;

            case AST_NODE_POSTFIX_SUBSCRIPT:
                if (type && (type->kind == TD_KIND_ARRAY || type->kind == TD_KIND_SLICE))
                {
                    type = type->element_type;
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

            case AST_NODE_POSTFIX_ASSERTION:
            {
                // Type assertion x.(T): only valid for 'any' type currently
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

    default:
        return NULL;
    }
}

// --- Statement analysis ---

static void
sem_analyse_return_statement(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type)
{
    if (node->list.count == 0)
    {
        if (expected_return_type != NULL && expected_return_type != type_descriptor_get_void_type(ctx->type_registry))
        {
            sem_error_list_add(&ctx->errors, node, "expected return value");
        }
        return;
    }

    odin_grammar_node_t * expr = node->list.children[0];
    TypeDescriptor const * expr_type = sem_evaluate_expr(ctx, expr);

    if (expected_return_type == NULL)
    {
        sem_error_list_add(&ctx->errors, node, "unexpected return value in void procedure");
        return;
    }

    if (expr_type != expected_return_type)
    {
        sem_error_list_add(&ctx->errors, node, "return type mismatch");
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
sem_analyse_procedure_literal(SemContext * ctx, odin_grammar_node_t * node)
{
    TypeDescriptor const * return_type = NULL;
    int param_count = 0;
    TypeDescriptor const ** param_types = NULL;
    size_t param_cap = 0;

    odin_grammar_node_t * param_list_node = NULL;
    odin_grammar_node_t * param_node = NULL; // single "Parameters" child of param_list_node
    odin_grammar_node_t * comp_stmt_node = NULL;

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
                if (sig_child->type == AST_NODE_RETURNS && sig_child->list.count > 0)
                {
                    odin_grammar_node_t * type_node = sig_child->list.children[0];
                    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
                    type_node->resolved_type = (TypeDescriptor *)td;
                    return_type = td;
                }
                else if (sig_child->type == AST_NODE_PARAMETER_LIST)
                {
                    param_list_node = sig_child;
                }
            }
        }
        else if (child->type == AST_NODE_COMPOUND_STATEMENT)
        {
            comp_stmt_node = child;
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

                // Parameter: KwUsing? (PolyIdent Colon)? Identifier Colon TypePrefix
                // The Identifier is the first non-NULL child with type AST_NODE_IDENTIFIER
                // The TypePrefix is the last child (always a type or identifier node)
                odin_grammar_node_t * param_ident = NULL;
                odin_grammar_node_t * param_type_node = NULL;
                for (size_t ci = 0; ci < param->list.count; ci++)
                {
                    odin_grammar_node_t * child = param->list.children[ci];
                    if (child == NULL)
                        continue;
                    if (child->type == AST_NODE_IDENTIFIER && param_ident == NULL)
                    {
                        param_ident = child;
                    }
                    else if (child->type == AST_NODE_IDENTIFIER || is_type_node(child))
                    {
                        param_type_node = child;
                    }
                }
                // If no explicit type node found, use the last identifier (e.g., `x: MyStruct`)
                if (param_type_node == NULL)
                {
                    // Find the last non-name identifier child
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

                if (param_count >= (int)param_cap)
                {
                    size_t new_cap = param_cap == 0 ? 4 : param_cap * 2;
                    TypeDescriptor const ** tmp = realloc(param_types, new_cap * sizeof(TypeDescriptor const *));
                    if (tmp == NULL)
                    {
                        free(param_types);
                        return;
                    }
                    param_types = tmp;
                    param_cap = new_cap;
                }
                param_types[param_count++] = pt;
            }
        }
    }

    // Push a new scope, register parameters, analyse body
    generator_push_scope(ctx->gen_ctx);

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
                    {
                        param_ident = child;
                    }
                    else if (child->type == AST_NODE_IDENTIFIER || is_type_node(child))
                    {
                        param_type_node = child;
                    }
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

                TypeDescriptor const * param_type = param_type_node->resolved_type;
                if (param_type == NULL)
                    continue;

                TypedValue tv = create_typed_value(NULL, param_type, true);
                generator_add_symbol(ctx->gen_ctx, param_ident->text, tv);
            }
        }
    }

    // Set resolved_type on the procedure literal
    {
        TypeDescriptor const * proc_type
            = get_or_create_proc_type(ctx->type_registry, return_type, param_types, param_count, NULL, 0, false);
        node->resolved_type = (TypeDescriptor *)proc_type;
    }

    free(param_types);

    if (comp_stmt_node)
    {
        sem_analyse_compound_statement(ctx, comp_stmt_node, return_type);
    }

    generator_pop_scope(ctx->gen_ctx);
}

// --- Top-level analysis ---

static void
sem_register_top_level_declaration(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->list.count < 2)
        return;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node->type != AST_NODE_IDENTIFIER)
        return;

    TypedValue tv = create_typed_value(NULL, NULL, false);
    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
}

static void
sem_register_top_level_variable(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->list.count < 1)
        return;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node->type != AST_NODE_IDENTIFIER)
        return;

    TypedValue tv = create_typed_value(NULL, NULL, true);
    scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
}

static void
sem_pass1_register_top_level(SemContext * ctx)
{
    odin_grammar_node_t * program = ctx->ast;
    if (program == NULL)
        return;

    for (size_t i = 0; i < program->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = program->list.children[i];
        if (ext_decl == NULL)
            continue;

        if (ext_decl->type == AST_NODE_EXTERNAL_DECLARATIONS)
        {
            for (size_t j = 0; j < ext_decl->list.count; j++)
            {
                odin_grammar_node_t * top_decl = ext_decl->list.children[j];
                if (top_decl == NULL)
                    continue;

                if (top_decl->type == AST_NODE_CONSTANT_DECL)
                {
                    sem_register_top_level_declaration(ctx, top_decl);
                }
                else if (top_decl->type == AST_NODE_VARIABLE_DECL)
                {
                    sem_register_top_level_variable(ctx, top_decl);
                }
            }
        }
    }
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
        for (size_t i = 0; i < node->list.count; i++)
        {
            sem_evaluate_expr(ctx, node->list.children[i]);
        }
        break;

    case AST_NODE_VARIABLE_DECL:
    {
        if (node->list.count < 1)
            break;
        odin_grammar_node_t * name_node = node->list.children[0];
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
            break;

        TypeDescriptor const * var_type = NULL;
        odin_grammar_node_t * type_node = NULL;
        odin_grammar_node_t * init_node = NULL;

        // Find type and init children (accounting for NULL terminal children)
        for (size_t i = 1; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (is_type_node(child))
                type_node = child;
            else if (child->type != AST_NODE_IDENTIFIER)
                init_node = child;
        }
        // For short decl `x := expr`, the expr may be the only non-NULL, non-name child
        if (type_node == NULL && init_node == NULL)
        {
            for (size_t i = 1; i < node->list.count; i++)
            {
                odin_grammar_node_t * child = node->list.children[i];
                if (child != NULL && child != name_node)
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
        if (init_node && type_node == NULL)
        {
            var_type = sem_evaluate_expr(ctx, init_node);
        }
        else if (init_node)
        {
            sem_evaluate_expr(ctx, init_node);
        }

        if (var_type && name_node)
        {
            TypedValue tv = create_typed_value(NULL, var_type, true);
            scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
        }
        break;
    }

    case AST_NODE_CONSTANT_DECL:
    {
        if (node->list.count < 2)
            break;
        odin_grammar_node_t * name_node = node->list.children[0];
        odin_grammar_node_t * value_node = node->list.children[1];
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
            break;
        if (value_node == NULL)
            break;

        if (value_node->type == AST_NODE_PROCEDURE_LITERAL)
        {
            sem_analyse_procedure_literal(ctx, value_node);
        }
        else
        {
            sem_evaluate_expr(ctx, value_node);
        }

        TypeDescriptor const * val_type = value_node->resolved_type;
        TypedValue tv = create_typed_value(NULL, val_type, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
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
            else
            {
                sem_evaluate_expr(ctx, child);
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

    default:
        break;
    }
}

static void
sem_pass2_analyse_bodies(SemContext * ctx)
{
    odin_grammar_node_t * program = ctx->ast;
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

    sem_pass2_analyse_bodies(ctx);
    if (sem_error_list_has_errors(&ctx->errors))
        return false;

    return true;
}
