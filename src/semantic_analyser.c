#include "semantic_analyser.h"

#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
sem_context_init(
    SemContext * ctx, odin_grammar_node_t * ast,
    TypeDescriptors * type_registry, GeneratorContext * gen_ctx
)
{
    ctx->ast = ast;
    ctx->type_registry = type_registry;
    ctx->gen_ctx = gen_ctx;
    sem_error_list_init(&ctx->errors);
}

// --- Forward declarations ---
static void sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type);

// --- Type expression resolution ---

static TypeDescriptor const *
sem_resolve_type_name(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->text == NULL) return NULL;
    return get_basic_type_by_name(ctx->type_registry, node->text);
}

static TypeDescriptor const *
sem_resolve_type_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

    switch (node->type)
    {
        case AST_NODE_BASIC_TYPE:
        case AST_NODE_TYPE_NAME:
            return sem_resolve_type_name(ctx, node);

        case AST_NODE_POINTER_TYPE:
        {
            TypeDescriptor const * pointee = sem_resolve_type_expr(ctx, node->list.children[0]);
            if (pointee == NULL) return NULL;
            return get_or_create_pointer_type(ctx->type_registry, pointee);
        }

        case AST_NODE_PROCEDURE_SIGNATURE:
        {
            TypeDescriptor const * return_type = NULL;
            int param_count = 0;
            TypeDescriptor const ** params = NULL;
            bool is_variadic = false;

            for (size_t i = 0; i < node->list.count; i++)
            {
                odin_grammar_node_t * child = node->list.children[i];
                if (child->type == AST_NODE_RETURNS && child->list.count > 0)
                {
                    return_type = sem_resolve_type_expr(ctx, child->list.children[0]);
                }
            }

            return get_or_create_proc_type(
                ctx->type_registry, return_type,
                params, param_count,
                NULL, 0, is_variadic
            );
        }

        default:
            return NULL;
    }
}

// --- Expression evaluation ---

static TypeDescriptor const *
sem_evaluate_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

    switch (node->type)
    {
        case AST_NODE_INTEGER_VALUE:
        case AST_NODE_FLOAT_VALUE:
        {
            TypeDescriptor const * int_type = get_basic_type_by_name(ctx->type_registry, "int");
            if (int_type)
            {
                node->resolved_type = (TypeDescriptor *)int_type;
            }
            return int_type;
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

        case AST_NODE_NIL:
        {
            node->resolved_type = NULL;
            return NULL;
        }

        case AST_NODE_IDENTIFIER:
        {
            symbol_t * sym = scope_find_symbol_entry(
                generator_current_scope(ctx->gen_ctx), node->text
            );
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

        case AST_NODE_MUL_EXPRESSION:
        case AST_NODE_ADD_EXPRESSION:
        case AST_NODE_COMP_EXPRESSION:
        case AST_NODE_LOG_AND_EXPRESSION:
        case AST_NODE_LOG_OR_EXPRESSION:
        case AST_NODE_SHIFT_EXPRESSION:
        case AST_NODE_BIT_AND_EXPRESSION:
        case AST_NODE_BIT_XOR_EXPRESSION:
        case AST_NODE_BIT_OR_EXPRESSION:
        case AST_NODE_RANGE_EXPRESSION:
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
            TypeDescriptor const * struct_type = sem_evaluate_expr(ctx, node->list.children[0]);
            if (struct_type && struct_type->kind == TD_KIND_STRUCT && node->list.children[1])
            {
                char const * field_name = node->list.children[1]->text;
                int idx = type_descriptor_find_struct_field_index(struct_type, field_name);
                if (idx >= 0)
                {
                    struct_field_t const * field = type_descriptor_get_struct_field(struct_type, idx);
                    node->resolved_type = (TypeDescriptor *)field->type_desc;
                    return field->type_desc;
                }
            }
            return NULL;
        }

        case AST_NODE_EXPRESSION:
        case AST_NODE_ASSIGN_EXPRESSION:
        case AST_NODE_OR_RETURN:
        case AST_NODE_OR_ELSE:
        case AST_NODE_TERNARY_EXPRESSION:
        case AST_NODE_PRIMARY_EXPRESSION:
        {
            if (node->list.count > 0)
            {
                return sem_evaluate_expr(ctx, node->list.children[0]);
            }
            return NULL;
        }

        case AST_NODE_POSTFIX_EXPRESSION:
        {
            if (node->list.count >= 1)
            {
                return sem_evaluate_expr(ctx, node->list.children[0]);
            }
            return NULL;
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
        if (expected_return_type != NULL
            && expected_return_type != type_descriptor_get_void_type(ctx->type_registry))
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
sem_analyse_compound_statement(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type)
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

    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child->type == AST_NODE_PROCEDURE_SIGNATURE)
        {
            for (size_t j = 0; j < child->list.count; j++)
            {
                odin_grammar_node_t * sig_child = child->list.children[j];
                if (sig_child->type == AST_NODE_RETURNS && sig_child->list.count > 0)
                {
                    odin_grammar_node_t * type_node = sig_child->list.children[0];
                    TypeDescriptor const * td = sem_resolve_type_expr(ctx, type_node);
                    type_node->resolved_type = (TypeDescriptor *)td;
                    return_type = td;
                }
            }
        }
    }

    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child->type == AST_NODE_COMPOUND_STATEMENT)
        {
            generator_push_scope(ctx->gen_ctx);
            sem_analyse_compound_statement(ctx, child, return_type);
            generator_pop_scope(ctx->gen_ctx);
        }
    }
}

// --- Top-level analysis ---

static void
sem_register_top_level_declaration(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->list.count < 2) return;

    odin_grammar_node_t * name_node = node->list.children[0];
    odin_grammar_node_t * value_node = node->list.children[1];

    if (name_node->type != AST_NODE_IDENTIFIER) return;

    if (value_node->type == AST_NODE_PROCEDURE_LITERAL)
    {
        TypedValue tv = create_typed_value(NULL, NULL, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), name_node->text, tv);
    }
}

static void
sem_pass1_register_top_level(SemContext * ctx)
{
    odin_grammar_node_t * program = ctx->ast;
    if (program == NULL) return;

    for (size_t i = 0; i < program->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = program->list.children[i];
        if (ext_decl == NULL) continue;

        if (ext_decl->type == AST_NODE_EXTERNAL_DECLARATIONS)
        {
            for (size_t j = 0; j < ext_decl->list.count; j++)
            {
                odin_grammar_node_t * top_decl = ext_decl->list.children[j];
                if (top_decl == NULL) continue;

                if (top_decl->type == AST_NODE_CONSTANT_DECL)
                {
                    sem_register_top_level_declaration(ctx, top_decl);
                }
            }
        }
    }
}

// --- Pass 2: body analysis dispatcher ---

static void
sem_pass2_node(SemContext * ctx, odin_grammar_node_t * node, TypeDescriptor const * expected_return_type)
{
    if (node == NULL) return;

    switch (node->type)
    {
        case AST_NODE_RETURN_STATEMENT:
            sem_analyse_return_statement(ctx, node, expected_return_type);
            break;

        case AST_NODE_COMPOUND_STATEMENT:
            sem_analyse_compound_statement(ctx, node, expected_return_type);
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

        default:
            break;
    }
}

static void
sem_pass2_analyse_bodies(SemContext * ctx)
{
    odin_grammar_node_t * program = ctx->ast;
    if (program == NULL) return;

    for (size_t i = 0; i < program->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = program->list.children[i];
        if (ext_decl == NULL || ext_decl->type != AST_NODE_EXTERNAL_DECLARATIONS) continue;

        for (size_t j = 0; j < ext_decl->list.count; j++)
        {
            odin_grammar_node_t * top_decl = ext_decl->list.children[j];
            if (top_decl == NULL || top_decl->type != AST_NODE_CONSTANT_DECL) continue;
            if (top_decl->list.count < 2) continue;

            odin_grammar_node_t * value_node = top_decl->list.children[1];
            if (value_node->type == AST_NODE_PROCEDURE_LITERAL)
            {
                sem_analyse_procedure_literal(ctx, value_node);
            }
        }
    }
}

// --- Main entry point ---

bool
sem_analyse(SemContext * ctx)
{
    sem_pass1_register_top_level(ctx);
    if (sem_error_list_has_errors(&ctx->errors)) return false;

    sem_pass2_analyse_bodies(ctx);
    if (sem_error_list_has_errors(&ctx->errors)) return false;

    return true;
}
