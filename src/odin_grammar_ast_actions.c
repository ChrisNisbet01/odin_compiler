#include "odin_grammar_ast.h"

#include "ast_metadata.h"
#include "odin_grammar_actions.h"
#include "odin_grammar_ast_actions.h"

#include <easy_pc/easy_pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
make_node(
    epc_ast_builder_ctx_t * ctx,
    epc_cpt_node_t * node,
    void ** children,
    int child_count,
    odin_grammar_node_type_t node_type,
    bool capture_text
)
{
    odin_grammar_node_t * result = calloc(1, sizeof(odin_grammar_node_t));
    result->type = node_type;

    result->list.children = calloc((size_t)child_count, sizeof(odin_grammar_node_t *));
    result->list.count = (size_t)child_count;
    for (int i = 0; i < child_count; i++)
    {
        result->list.children[i] = (odin_grammar_node_t *)children[i];
    }

    epc_parser_input_view_t input_view = epc_cpt_node_get_input_view(node);
    result->source_data.view = input_view;

    if (capture_text)
    {
        char const * sem = epc_cpt_node_get_semantic_content(node);
        size_t sem_len = epc_cpt_node_get_semantic_len(node);
        if (sem != NULL && sem_len > 0)
        {
            result->text = strndup(sem, sem_len);
            // Strip trailing whitespace (lexeme combinator with '#'
            // treats bash comments as whitespace, including it in the match)
            size_t tlen = strlen(result->text);
            while (tlen > 0
                   && (result->text[tlen - 1] == ' ' || result->text[tlen - 1] == '\t' || result->text[tlen - 1] == '\n'
                       || result->text[tlen - 1] == '\r'))
            {
                tlen--;
            }
            size_t orig_len = strlen(result->text);
            if (tlen < orig_len)
            {
                char * trimmed = strndup(result->text, tlen);
                free((void *)result->text);
                result->text = trimmed;
            }
        }
    }

    epc_ast_push(ctx, result);
}

static odin_grammar_node_t *
make_node_base(epc_cpt_node_t * node, void ** children, int count)
{
    (void)node;

    odin_grammar_node_t * result = calloc(1, sizeof(*result));
    result->source_data.view = epc_cpt_node_get_input_view(node);
    result->list.count = (size_t)count;
    result->list.children = calloc((size_t)count, sizeof(odin_grammar_node_t *));
    for (int i = 0; i < count; i++)
    {
        result->list.children[i] = (odin_grammar_node_t *)children[i];
    }
    return result;
}

static OperatorKind
determine_operator_kind(odin_grammar_node_type_t node_type, char const * sem, size_t sem_len)
{
    if (sem == NULL || sem_len == 0)
        return OP_INVALID;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (node_type)
    {
    case AST_NODE_ADD_OP:
        if (sem_len == 1)
        {
            if (sem[0] == '+')
                return OP_ADD;
            if (sem[0] == '-')
                return OP_SUB;
        }
        return OP_INVALID;

    case AST_NODE_MUL_OP:
        if (sem_len == 1)
        {
            if (sem[0] == '*')
                return OP_MUL;
            if (sem[0] == '/')
                return OP_DIV;
            if (sem[0] == '%')
                return OP_MOD;
        }
        if (sem_len == 3 && strncmp(sem, "mod", 3) == 0)
            return OP_MOD;
        return OP_INVALID;

    case AST_NODE_SHIFT_OP:
        if (sem_len == 2)
        {
            if (sem[0] == '<' && sem[1] == '<')
                return OP_SHL;
            if (sem[0] == '>' && sem[1] == '>')
                return OP_SHR;
        }
        return OP_INVALID;

    case AST_NODE_BIT_AND_OP:
        if (sem_len == 1 && sem[0] == '&')
            return OP_BIT_AND;
        return OP_INVALID;

    case AST_NODE_BIT_XOR_OP:
        if (sem_len == 1 && sem[0] == '~')
            return OP_BIT_XOR;
        return OP_INVALID;

    case AST_NODE_BIT_OR_OP:
        if (sem_len == 1 && sem[0] == '|')
            return OP_BIT_OR;
        return OP_INVALID;

    case AST_NODE_COMP_OP:
    {
        if (sem_len == 2)
        {
            if (sem[0] == '=' && sem[1] == '=')
                return OP_EQ;
            if (sem[0] == '!' && sem[1] == '=')
                return OP_NE;
            if (sem[0] == '<' && sem[1] == '=')
                return OP_LE;
            if (sem[0] == '>' && sem[1] == '=')
                return OP_GE;
        }
        if (sem_len == 1)
        {
            if (sem[0] == '<')
                return OP_LT;
            if (sem[0] == '>')
                return OP_GT;
        }
        if (sem_len == 2 && strncmp(sem, "in", 2) == 0)
            return OP_IN;
        if (sem_len == 6 && strncmp(sem, "not_in", 6) == 0)
            return OP_NOT_IN;
        return OP_INVALID;
    }

    case AST_NODE_LOG_AND_OP:
        if ((sem_len == 2 && sem[0] == '&' && sem[1] == '&') || (sem_len == 3 && strncmp(sem, "and", 3) == 0))
            return OP_LOG_AND;
        return OP_INVALID;

    case AST_NODE_LOG_OR_OP:
        if ((sem_len == 2 && sem[0] == '|' && sem[1] == '|') || (sem_len == 2 && strncmp(sem, "or", 2) == 0))
            return OP_LOG_OR;
        return OP_INVALID;

    case AST_NODE_RANGE_OP:
        if (sem_len == 2 && sem[0] == '.' && sem[1] == '.')
            return OP_RANGE;
        if (sem_len == 3 && sem[0] == '.' && sem[1] == '.' && sem[2] == '<')
            return OP_RANGE_HALF;
        return OP_INVALID;

    case AST_NODE_ASSIGN_OP:
    {
        if (sem_len == 1 && sem[0] == '=')
            return OP_ASSIGN;
        if (sem_len == 2)
        {
            if (sem[0] == '+')
                return OP_ADD_ASSIGN;
            if (sem[0] == '-')
                return OP_SUB_ASSIGN;
            if (sem[0] == '*')
                return OP_MUL_ASSIGN;
            if (sem[0] == '/')
                return OP_DIV_ASSIGN;
            if (sem[0] == '%')
                return OP_MOD_ASSIGN;
            if (sem[0] == '&')
                return OP_AND_ASSIGN;
            if (sem[0] == '|')
                return OP_OR_ASSIGN;
            if (sem[0] == '~')
                return OP_XOR_ASSIGN;
        }
        if (sem_len == 3)
        {
            if (sem[0] == '<' && sem[1] == '<')
                return OP_SHL_ASSIGN;
            if (sem[0] == '>' && sem[1] == '>')
                return OP_SHR_ASSIGN;
        }
        return OP_INVALID;
    }

    case AST_NODE_UNARY_OP:
    {
        if (sem[0] == '!' || (sem_len == 3 && strncmp(sem, "not", 3) == 0))
            return OP_UNARY_NOT;
        if (sem_len == 1)
        {
            if (sem[0] == '-')
                return OP_UNARY_NEG;
            if (sem[0] == '+')
                return OP_UNARY_POS;
            if (sem[0] == '~')
                return OP_UNARY_XOR;
            if (sem[0] == '&')
                return OP_UNARY_ADDR;
            if (sem[0] == '^')
                return OP_UNARY_DEREF;
        }
        return OP_INVALID;
    }

    default:
        return OP_INVALID;
    }

#pragma GCC diagnostic pop
}

#define DEFINE_OP_ACTION(name, node_type)                                                                              \
    static void name(                                                                                                  \
        epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data              \
    )                                                                                                                  \
    {                                                                                                                  \
        (void)user_data;                                                                                               \
        odin_grammar_node_t * result = make_node_base(node, children, count);                                          \
        result->type = node_type;                                                                                      \
        char const * sem = epc_cpt_node_get_semantic_content(node);                                                    \
        size_t sem_len = epc_cpt_node_get_semantic_len(node);                                                          \
        AstOpMetadata * m = calloc(1, sizeof(AstOpMetadata));                                                          \
        m->kind = determine_operator_kind(node_type, sem, sem_len);                                                    \
        result->metadata = m;                                                                                          \
        epc_ast_push(ctx, result);                                                                                     \
    }

#define DEFINE_ACTION(name, node_type)                                                                                 \
    static void name(                                                                                                  \
        epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data              \
    )                                                                                                                  \
    {                                                                                                                  \
        (void)user_data;                                                                                               \
        make_node(ctx, node, children, count, node_type, false);                                                       \
    }

#define DEFINE_TERMINAL_ACTION(name, node_type)                                                                        \
    static void name(                                                                                                  \
        epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data              \
    )                                                                                                                  \
    {                                                                                                                  \
        (void)user_data;                                                                                               \
        make_node(ctx, node, children, count, node_type, true);                                                        \
    }

// --- Structural nodes (no text captured) ---
DEFINE_ACTION(ast_action_program_action, AST_NODE_PROGRAM)
DEFINE_ACTION(ast_action_external_declarations_action, AST_NODE_EXTERNAL_DECLARATIONS)
DEFINE_ACTION(ast_action_package_clause_action, AST_NODE_PACKAGE_CLAUSE)
DEFINE_ACTION(ast_action_import_action, AST_NODE_IMPORT)
DEFINE_ACTION(ast_action_import_using_action, AST_NODE_IMPORT_USING)
DEFINE_ACTION(ast_action_import_named_action, AST_NODE_IMPORT_NAMED)
DEFINE_ACTION(ast_action_pointer_type_action, AST_NODE_POINTER_TYPE)
DEFINE_ACTION(ast_action_array_type_action, AST_NODE_ARRAY_TYPE)
DEFINE_ACTION(ast_action_dynamic_array_type_action, AST_NODE_DYNAMIC_ARRAY_TYPE)
DEFINE_ACTION(ast_action_slice_type_action, AST_NODE_SLICE_TYPE)
DEFINE_ACTION(ast_action_map_type_action, AST_NODE_MAP_TYPE)
DEFINE_ACTION(ast_action_soa_type_action, AST_NODE_SOA_TYPE)
DEFINE_ACTION(ast_action_enum_type_action, AST_NODE_ENUM_TYPE)
DEFINE_ACTION(ast_action_enum_type_ref_action, AST_NODE_ENUM_TYPE_REF)
DEFINE_ACTION(ast_action_union_type_action, AST_NODE_UNION_TYPE)
DEFINE_ACTION(ast_action_bit_field_type_action, AST_NODE_BIT_FIELD_TYPE)
DEFINE_ACTION(ast_action_bit_set_type_action, AST_NODE_BIT_SET_TYPE)
DEFINE_TERMINAL_ACTION(ast_action_bit_set_range_action, AST_NODE_BIT_SET_RANGE)
DEFINE_ACTION(ast_action_struct_type_action, AST_NODE_STRUCT_TYPE)
DEFINE_ACTION(ast_action_struct_type_ref_action, AST_NODE_STRUCT_TYPE_REF)
DEFINE_ACTION(ast_action_distinct_type_action, AST_NODE_DISTINCT_TYPE)
DEFINE_ACTION(ast_action_procedure_signature_action, AST_NODE_PROCEDURE_SIGNATURE)
DEFINE_ACTION(ast_action_parameter_action, AST_NODE_PARAMETER)
DEFINE_ACTION(ast_action_parameters_action, AST_NODE_PARAMETERS)
DEFINE_ACTION(ast_action_parameter_list_action, AST_NODE_PARAMETER_LIST)
DEFINE_ACTION(ast_action_named_return_action, AST_NODE_NAMED_RETURN)
DEFINE_ACTION(ast_action_return_list_action, AST_NODE_RETURN_LIST)
DEFINE_ACTION(ast_action_return_type_list_action, AST_NODE_RETURN_TYPE_LIST)
DEFINE_ACTION(ast_action_returns_action, AST_NODE_RETURNS)
DEFINE_ACTION(ast_action_where_clause_action, AST_NODE_WHERE_CLAUSE)
DEFINE_ACTION(ast_action_calling_convention_action, AST_NODE_CALLING_CONVENTION)
DEFINE_ACTION(ast_action_context_expr_action, AST_NODE_CONTEXT_EXPR)
DEFINE_ACTION(ast_action_primary_expression_action, AST_NODE_PRIMARY_EXPRESSION)
DEFINE_ACTION(ast_action_postfix_call_action, AST_NODE_POSTFIX_CALL)
DEFINE_ACTION(ast_action_postfix_subscript_action, AST_NODE_POSTFIX_SUBSCRIPT)
DEFINE_TERMINAL_ACTION(ast_action_postfix_slice_action, AST_NODE_POSTFIX_SLICE)
DEFINE_TERMINAL_ACTION(ast_action_postfix_slice_lt_action, AST_NODE_POSTFIX_SLICE_LT)
DEFINE_ACTION(ast_action_postfix_member_action, AST_NODE_POSTFIX_MEMBER)
DEFINE_ACTION(ast_action_postfix_deref_action, AST_NODE_POSTFIX_DEREF)
DEFINE_ACTION(ast_action_postfix_assertion_action, AST_NODE_POSTFIX_ASSERTION)
DEFINE_ACTION(ast_action_postfix_ops_action, AST_NODE_POSTFIX_OPS)
DEFINE_ACTION(ast_action_postfix_expression_action, AST_NODE_POSTFIX_EXPRESSION)
DEFINE_OP_ACTION(ast_action_unary_op_action, AST_NODE_UNARY_OP)
DEFINE_ACTION(ast_action_unary_prefix_action, AST_NODE_UNARY_EXPRESSION)
DEFINE_OP_ACTION(ast_action_mul_op_action, AST_NODE_MUL_OP)
DEFINE_ACTION(ast_action_mul_expression_action, AST_NODE_MUL_EXPRESSION)
DEFINE_OP_ACTION(ast_action_add_op_action, AST_NODE_ADD_OP)
DEFINE_ACTION(ast_action_add_expression_action, AST_NODE_ADD_EXPRESSION)
DEFINE_OP_ACTION(ast_action_shift_op_action, AST_NODE_SHIFT_OP)
DEFINE_ACTION(ast_action_shift_expression_action, AST_NODE_SHIFT_EXPRESSION)
DEFINE_OP_ACTION(ast_action_bit_and_op_action, AST_NODE_BIT_AND_OP)
DEFINE_ACTION(ast_action_bit_and_expression_action, AST_NODE_BIT_AND_EXPRESSION)
DEFINE_OP_ACTION(ast_action_bit_xor_op_action, AST_NODE_BIT_XOR_OP)
DEFINE_ACTION(ast_action_bit_xor_expression_action, AST_NODE_BIT_XOR_EXPRESSION)
DEFINE_OP_ACTION(ast_action_bit_or_op_action, AST_NODE_BIT_OR_OP)
DEFINE_ACTION(ast_action_bit_or_expression_action, AST_NODE_BIT_OR_EXPRESSION)
DEFINE_OP_ACTION(ast_action_comp_op_action, AST_NODE_COMP_OP)
DEFINE_ACTION(ast_action_comp_expression_action, AST_NODE_COMP_EXPRESSION)
DEFINE_OP_ACTION(ast_action_log_and_op_action, AST_NODE_LOG_AND_OP)
DEFINE_ACTION(ast_action_log_and_expression_action, AST_NODE_LOG_AND_EXPRESSION)
DEFINE_OP_ACTION(ast_action_log_or_op_action, AST_NODE_LOG_OR_OP)
DEFINE_ACTION(ast_action_log_or_expression_action, AST_NODE_LOG_OR_EXPRESSION)
DEFINE_OP_ACTION(ast_action_range_op_action, AST_NODE_RANGE_OP)
DEFINE_ACTION(ast_action_range_expression_action, AST_NODE_RANGE_EXPRESSION)
DEFINE_ACTION(ast_action_ternary_expression_action, AST_NODE_TERNARY_EXPRESSION)
DEFINE_ACTION(ast_action_or_else_action, AST_NODE_OR_ELSE)
DEFINE_ACTION(ast_action_or_return_action, AST_NODE_OR_RETURN)
DEFINE_OP_ACTION(ast_action_assign_op_action, AST_NODE_ASSIGN_OP)
DEFINE_ACTION(ast_action_assign_expression_action, AST_NODE_ASSIGN_EXPRESSION)
DEFINE_ACTION(ast_action_expression_action, AST_NODE_EXPRESSION)
DEFINE_ACTION(ast_action_argument_list_action, AST_NODE_ARGUMENT_LIST)
DEFINE_ACTION(ast_action_compound_statement_action, AST_NODE_COMPOUND_STATEMENT)
DEFINE_ACTION(ast_action_defer_statement_action, AST_NODE_DEFER_STATEMENT)
DEFINE_ACTION(ast_action_when_statement_action, AST_NODE_WHEN_STATEMENT)
DEFINE_ACTION(ast_action_if_statement_action, AST_NODE_IF_STATEMENT)
DEFINE_ACTION(ast_action_for_statement_action, AST_NODE_FOR_STATEMENT)
DEFINE_ACTION(ast_action_switch_statement_action, AST_NODE_SWITCH_STATEMENT)
DEFINE_ACTION(ast_action_switch_case_action, AST_NODE_SWITCH_CASE)
DEFINE_ACTION(ast_action_switch_default_action, AST_NODE_SWITCH_DEFAULT)
DEFINE_ACTION(ast_action_return_statement_action, AST_NODE_RETURN_STATEMENT)
DEFINE_ACTION(ast_action_break_statement_action, AST_NODE_BREAK_STATEMENT)
DEFINE_ACTION(ast_action_continue_statement_action, AST_NODE_CONTINUE_STATEMENT)
DEFINE_ACTION(ast_action_fallthrough_statement_action, AST_NODE_FALLTHROUGH_STATEMENT)
DEFINE_ACTION(ast_action_assign_statement_action, AST_NODE_ASSIGN_STATEMENT)
DEFINE_ACTION(ast_action_expression_statement_action, AST_NODE_EXPRESSION_STATEMENT)
DEFINE_ACTION(ast_action_procedure_literal_action, AST_NODE_PROCEDURE_LITERAL)
DEFINE_ACTION(ast_action_identifier_list_action, AST_NODE_IDENTIFIER_LIST)
DEFINE_ACTION(ast_action_variable_decl_action, AST_NODE_VARIABLE_DECL)
DEFINE_ACTION(ast_action_constant_decl_action, AST_NODE_CONSTANT_DECL)
DEFINE_ACTION(ast_action_foreign_import_action, AST_NODE_FOREIGN_IMPORT)
DEFINE_ACTION(ast_action_foreign_block_action, AST_NODE_FOREIGN_BLOCK)
DEFINE_ACTION(ast_action_when_decl_action, AST_NODE_WHEN_DECL)
DEFINE_ACTION(ast_action_when_body_action, AST_NODE_WHEN_BODY)
DEFINE_ACTION(ast_action_using_decl_action, AST_NODE_USING_DECL)
DEFINE_ACTION(ast_action_enumerator_action, AST_NODE_ENUMERATOR)
DEFINE_ACTION(ast_action_enumerator_list_action, AST_NODE_ENUMERATOR_LIST)
DEFINE_ACTION(ast_action_union_field_action, AST_NODE_UNION_FIELD)
DEFINE_ACTION(ast_action_union_field_list_action, AST_NODE_UNION_FIELD_LIST)
DEFINE_ACTION(ast_action_bit_field_field_action, AST_NODE_BIT_FIELD_FIELD)
DEFINE_ACTION(ast_action_bit_field_field_list_action, AST_NODE_BIT_FIELD_FIELD_LIST)
static void
ast_action_struct_field_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)user_data;
    odin_grammar_node_t * result = calloc(1, sizeof(odin_grammar_node_t));
    result->source_data.view = epc_cpt_node_get_input_view(node);
    result->type = AST_NODE_STRUCT_FIELD;
    result->list.children = calloc((size_t)count, sizeof(odin_grammar_node_t *));
    result->list.count = (size_t)count;
    for (int i = 0; i < count; i++)
    {
        result->list.children[i] = (odin_grammar_node_t *)children[i];
    }
    // Detect "using" keyword from CPT content (stored as text on the struct field node)
    char const * content = epc_cpt_node_get_content(node);
    if (content != NULL && content[0] == 'u' && strncmp(content, "using", 5) == 0)
    {
        result->text = strdup("using");
    }
    epc_ast_push(ctx, result);
}
DEFINE_ACTION(ast_action_struct_field_list_action, AST_NODE_STRUCT_FIELD_LIST)

DEFINE_ACTION(ast_action_auto_cast_expr_action, AST_NODE_AUTO_CAST_EXPR)
DEFINE_ACTION(ast_action_cast_expr_action, AST_NODE_CAST_EXPR)
DEFINE_ACTION(ast_action_transmute_expr_action, AST_NODE_TRANSMUTE_EXPR)
DEFINE_ACTION(ast_action_len_expr_action, AST_NODE_LEN_EXPR)
DEFINE_ACTION(ast_action_cap_expr_action, AST_NODE_CAP_EXPR)
DEFINE_ACTION(ast_action_size_of_expr_action, AST_NODE_SIZE_OF_EXPR)
DEFINE_ACTION(ast_action_align_of_expr_action, AST_NODE_ALIGN_OF_EXPR)
DEFINE_ACTION(ast_action_offset_of_expr_action, AST_NODE_OFFSET_OF_EXPR)
DEFINE_ACTION(ast_action_raw_data_expr_action, AST_NODE_RAW_DATA_EXPR)
DEFINE_ACTION(ast_action_min_expr_action, AST_NODE_MIN_EXPR)
DEFINE_ACTION(ast_action_max_expr_action, AST_NODE_MAX_EXPR)
DEFINE_ACTION(ast_action_type_of_expr_action, AST_NODE_TYPE_OF_EXPR)
DEFINE_ACTION(ast_action_make_expr_action, AST_NODE_MAKE_EXPR)
DEFINE_ACTION(ast_action_new_expr_action, AST_NODE_NEW_EXPR)
DEFINE_ACTION(ast_action_delete_expr_action, AST_NODE_DELETE_EXPR)
DEFINE_ACTION(ast_action_incl_expr_action, AST_NODE_INCL_EXPR)
DEFINE_ACTION(ast_action_excl_expr_action, AST_NODE_EXCL_EXPR)
DEFINE_ACTION(ast_action_complex_expr_action, AST_NODE_COMPLEX_EXPR)
DEFINE_ACTION(ast_action_quaternion_expr_action, AST_NODE_QUATERNION_EXPR)
DEFINE_ACTION(ast_action_maybe_type_action, AST_NODE_MAYBE_TYPE)

// --- Terminal nodes (text captured for semantic use) ---
DEFINE_TERMINAL_ACTION(ast_action_identifier_action, AST_NODE_IDENTIFIER)
DEFINE_TERMINAL_ACTION(ast_action_poly_ident_action, AST_NODE_POLY_IDENT)
DEFINE_TERMINAL_ACTION(ast_action_basic_type_action, AST_NODE_BASIC_TYPE)
DEFINE_TERMINAL_ACTION(ast_action_type_name_action, AST_NODE_TYPE_NAME)
DEFINE_TERMINAL_ACTION(ast_action_integer_base_action, AST_NODE_INTEGER_BASE)
DEFINE_TERMINAL_ACTION(ast_action_integer_value_action, AST_NODE_INTEGER_VALUE)
DEFINE_TERMINAL_ACTION(ast_action_float_base_action, AST_NODE_FLOAT_BASE)
DEFINE_TERMINAL_ACTION(ast_action_float_value_action, AST_NODE_FLOAT_VALUE)
DEFINE_TERMINAL_ACTION(ast_action_string_literal_action, AST_NODE_STRING_LITERAL)
DEFINE_TERMINAL_ACTION(ast_action_raw_string_literal_action, AST_NODE_RAW_STRING_LITERAL)
DEFINE_TERMINAL_ACTION(ast_action_rune_literal_action, AST_NODE_RUNE_LITERAL)
DEFINE_TERMINAL_ACTION(ast_action_bool_true_action, AST_NODE_BOOL_TRUE)
DEFINE_TERMINAL_ACTION(ast_action_bool_false_action, AST_NODE_BOOL_FALSE)
DEFINE_TERMINAL_ACTION(ast_action_nil_action, AST_NODE_NIL)
DEFINE_TERMINAL_ACTION(ast_action_none_action, AST_NODE_NONE)
DEFINE_TERMINAL_ACTION(ast_action_ellipsis_action, AST_NODE_ELLIPSIS)
DEFINE_TERMINAL_ACTION(ast_action_triple_dash_action, AST_NODE_TRIPLE_DASH)
DEFINE_TERMINAL_ACTION(ast_action_directive_action, AST_NODE_DIRECTIVE)
DEFINE_TERMINAL_ACTION(ast_action_directive_with_args_action, AST_NODE_DIRECTIVE_WITH_ARGS)

#undef DEFINE_ACTION
#undef DEFINE_TERMINAL_ACTION

void
odin_grammar_ast_hook_registry_init(epc_ast_hook_registry_t * registry)
{
    epc_ast_hook_registry_set_free_node(registry, odin_grammar_node_free);

#define REGISTER(id, cb) epc_ast_hook_registry_set_action(registry, id, cb)

    REGISTER(AST_ACTION_PROGRAM, ast_action_program_action);
    REGISTER(AST_ACTION_EXTERNAL_DECLARATIONS, ast_action_external_declarations_action);
    REGISTER(AST_ACTION_PACKAGE_CLAUSE, ast_action_package_clause_action);
    REGISTER(AST_ACTION_IMPORT, ast_action_import_action);
    REGISTER(AST_ACTION_IMPORT_USING, ast_action_import_using_action);
    REGISTER(AST_ACTION_IMPORT_NAMED, ast_action_import_named_action);
    REGISTER(AST_ACTION_IDENTIFIER, ast_action_identifier_action);
    REGISTER(AST_ACTION_POLY_IDENT, ast_action_poly_ident_action);
    REGISTER(AST_ACTION_BASIC_TYPE, ast_action_basic_type_action);
    REGISTER(AST_ACTION_POINTER_TYPE, ast_action_pointer_type_action);
    REGISTER(AST_ACTION_ARRAY_TYPE, ast_action_array_type_action);
    REGISTER(AST_ACTION_DYNAMIC_ARRAY_TYPE, ast_action_dynamic_array_type_action);
    REGISTER(AST_ACTION_SLICE_TYPE, ast_action_slice_type_action);
    REGISTER(AST_ACTION_MAP_TYPE, ast_action_map_type_action);
    REGISTER(AST_ACTION_SOA_TYPE, ast_action_soa_type_action);
    REGISTER(AST_ACTION_ENUM_TYPE, ast_action_enum_type_action);
    REGISTER(AST_ACTION_ENUM_TYPE_REF, ast_action_enum_type_ref_action);
    REGISTER(AST_ACTION_UNION_TYPE, ast_action_union_type_action);
    REGISTER(AST_ACTION_BIT_FIELD_TYPE, ast_action_bit_field_type_action);
    REGISTER(AST_ACTION_BIT_SET_TYPE, ast_action_bit_set_type_action);
    REGISTER(AST_ACTION_BIT_SET_RANGE, ast_action_bit_set_range_action);
    REGISTER(AST_ACTION_STRUCT_TYPE, ast_action_struct_type_action);
    REGISTER(AST_ACTION_STRUCT_TYPE_REF, ast_action_struct_type_ref_action);
    REGISTER(AST_ACTION_TYPE_NAME, ast_action_type_name_action);
    REGISTER(AST_ACTION_DISTINCT_TYPE, ast_action_distinct_type_action);
    REGISTER(AST_ACTION_PROCEDURE_SIGNATURE, ast_action_procedure_signature_action);
    REGISTER(AST_ACTION_PARAMETER, ast_action_parameter_action);
    REGISTER(AST_ACTION_PARAMETERS, ast_action_parameters_action);
    REGISTER(AST_ACTION_PARAMETER_LIST, ast_action_parameter_list_action);
    REGISTER(AST_ACTION_NAMED_RETURN, ast_action_named_return_action);
    REGISTER(AST_ACTION_RETURN_LIST, ast_action_return_list_action);
    REGISTER(AST_ACTION_RETURN_TYPE_LIST, ast_action_return_type_list_action);
    REGISTER(AST_ACTION_RETURNS, ast_action_returns_action);
    REGISTER(AST_ACTION_WHERE_CLAUSE, ast_action_where_clause_action);
    REGISTER(AST_ACTION_CALLING_CONVENTION, ast_action_calling_convention_action);
    REGISTER(AST_ACTION_CONTEXT_EXPR, ast_action_context_expr_action);
    REGISTER(AST_ACTION_PRIMARY_EXPRESSION, ast_action_primary_expression_action);
    REGISTER(AST_ACTION_POSTFIX_CALL, ast_action_postfix_call_action);
    REGISTER(AST_ACTION_POSTFIX_SUBSCRIPT, ast_action_postfix_subscript_action);
    REGISTER(AST_ACTION_POSTFIX_SLICE, ast_action_postfix_slice_action);
    REGISTER(AST_ACTION_POSTFIX_SLICE_LT, ast_action_postfix_slice_lt_action);
    REGISTER(AST_ACTION_POSTFIX_MEMBER, ast_action_postfix_member_action);
    REGISTER(AST_ACTION_POSTFIX_DEREF, ast_action_postfix_deref_action);
    REGISTER(AST_ACTION_POSTFIX_ASSERTION, ast_action_postfix_assertion_action);
    REGISTER(AST_ACTION_POSTFIX_OPS, ast_action_postfix_ops_action);
    REGISTER(AST_ACTION_POSTFIX_EXPRESSION, ast_action_postfix_expression_action);
    REGISTER(AST_ACTION_UNARY_OP, ast_action_unary_op_action);
    REGISTER(AST_ACTION_UNARY_PREFIX, ast_action_unary_prefix_action);
    REGISTER(AST_ACTION_MUL_OP, ast_action_mul_op_action);
    REGISTER(AST_ACTION_MUL_EXPRESSION, ast_action_mul_expression_action);
    REGISTER(AST_ACTION_ADD_OP, ast_action_add_op_action);
    REGISTER(AST_ACTION_ADD_EXPRESSION, ast_action_add_expression_action);
    REGISTER(AST_ACTION_SHIFT_OP, ast_action_shift_op_action);
    REGISTER(AST_ACTION_SHIFT_EXPRESSION, ast_action_shift_expression_action);
    REGISTER(AST_ACTION_BIT_AND_OP, ast_action_bit_and_op_action);
    REGISTER(AST_ACTION_BIT_AND_EXPRESSION, ast_action_bit_and_expression_action);
    REGISTER(AST_ACTION_BIT_XOR_OP, ast_action_bit_xor_op_action);
    REGISTER(AST_ACTION_BIT_XOR_EXPRESSION, ast_action_bit_xor_expression_action);
    REGISTER(AST_ACTION_BIT_OR_OP, ast_action_bit_or_op_action);
    REGISTER(AST_ACTION_BIT_OR_EXPRESSION, ast_action_bit_or_expression_action);
    REGISTER(AST_ACTION_COMP_OP, ast_action_comp_op_action);
    REGISTER(AST_ACTION_COMP_EXPRESSION, ast_action_comp_expression_action);
    REGISTER(AST_ACTION_LOG_AND_OP, ast_action_log_and_op_action);
    REGISTER(AST_ACTION_LOG_AND_EXPRESSION, ast_action_log_and_expression_action);
    REGISTER(AST_ACTION_LOG_OR_OP, ast_action_log_or_op_action);
    REGISTER(AST_ACTION_LOG_OR_EXPRESSION, ast_action_log_or_expression_action);
    REGISTER(AST_ACTION_RANGE_OP, ast_action_range_op_action);
    REGISTER(AST_ACTION_RANGE_EXPRESSION, ast_action_range_expression_action);
    REGISTER(AST_ACTION_TERNARY_EXPRESSION, ast_action_ternary_expression_action);
    REGISTER(AST_ACTION_OR_ELSE, ast_action_or_else_action);
    REGISTER(AST_ACTION_OR_RETURN, ast_action_or_return_action);
    REGISTER(AST_ACTION_ASSIGN_OP, ast_action_assign_op_action);
    REGISTER(AST_ACTION_ASSIGN_EXPRESSION, ast_action_assign_expression_action);
    REGISTER(AST_ACTION_EXPRESSION, ast_action_expression_action);
    REGISTER(AST_ACTION_ARGUMENT_LIST, ast_action_argument_list_action);
    REGISTER(AST_ACTION_COMPOUND_STATEMENT, ast_action_compound_statement_action);
    REGISTER(AST_ACTION_DEFER_STATEMENT, ast_action_defer_statement_action);
    REGISTER(AST_ACTION_WHEN_STATEMENT, ast_action_when_statement_action);
    REGISTER(AST_ACTION_IF_STATEMENT, ast_action_if_statement_action);
    REGISTER(AST_ACTION_FOR_STATEMENT, ast_action_for_statement_action);
    REGISTER(AST_ACTION_SWITCH_STATEMENT, ast_action_switch_statement_action);
    REGISTER(AST_ACTION_SWITCH_CASE, ast_action_switch_case_action);
    REGISTER(AST_ACTION_SWITCH_DEFAULT, ast_action_switch_default_action);
    REGISTER(AST_ACTION_RETURN_STATEMENT, ast_action_return_statement_action);
    REGISTER(AST_ACTION_BREAK_STATEMENT, ast_action_break_statement_action);
    REGISTER(AST_ACTION_CONTINUE_STATEMENT, ast_action_continue_statement_action);
    REGISTER(AST_ACTION_FALLTHROUGH_STATEMENT, ast_action_fallthrough_statement_action);
    REGISTER(AST_ACTION_ASSIGN_STATEMENT, ast_action_assign_statement_action);
    REGISTER(AST_ACTION_EXPRESSION_STATEMENT, ast_action_expression_statement_action);
    REGISTER(AST_ACTION_PROCEDURE_LITERAL, ast_action_procedure_literal_action);
    REGISTER(AST_ACTION_IDENTIFIER_LIST, ast_action_identifier_list_action);
    REGISTER(AST_ACTION_VARIABLE_DECL, ast_action_variable_decl_action);
    REGISTER(AST_ACTION_CONSTANT_DECL, ast_action_constant_decl_action);
    REGISTER(AST_ACTION_FOREIGN_IMPORT, ast_action_foreign_import_action);
    REGISTER(AST_ACTION_FOREIGN_BLOCK, ast_action_foreign_block_action);
    REGISTER(AST_ACTION_WHEN_DECL, ast_action_when_decl_action);
    REGISTER(AST_ACTION_WHEN_BODY, ast_action_when_body_action);
    REGISTER(AST_ACTION_USING_DECL, ast_action_using_decl_action);
    REGISTER(AST_ACTION_DIRECTIVE, ast_action_directive_action);
    REGISTER(AST_ACTION_DIRECTIVE_WITH_ARGS, ast_action_directive_with_args_action);
    REGISTER(AST_ACTION_INTEGER_BASE, ast_action_integer_base_action);
    REGISTER(AST_ACTION_INTEGER_VALUE, ast_action_integer_value_action);
    REGISTER(AST_ACTION_FLOAT_BASE, ast_action_float_base_action);
    REGISTER(AST_ACTION_FLOAT_VALUE, ast_action_float_value_action);
    REGISTER(AST_ACTION_STRING_LITERAL, ast_action_string_literal_action);
    REGISTER(AST_ACTION_RAW_STRING_LITERAL, ast_action_raw_string_literal_action);
    REGISTER(AST_ACTION_RUNE_LITERAL, ast_action_rune_literal_action);
    REGISTER(AST_ACTION_BOOL_TRUE, ast_action_bool_true_action);
    REGISTER(AST_ACTION_BOOL_FALSE, ast_action_bool_false_action);
    REGISTER(AST_ACTION_NIL, ast_action_nil_action);
    REGISTER(AST_ACTION_NONE, ast_action_none_action);
    REGISTER(AST_ACTION_ELLIPSIS, ast_action_ellipsis_action);
    REGISTER(AST_ACTION_TRIPLE_DASH, ast_action_triple_dash_action);
    REGISTER(AST_ACTION_ENUMERATOR, ast_action_enumerator_action);
    REGISTER(AST_ACTION_ENUMERATOR_LIST, ast_action_enumerator_list_action);
    REGISTER(AST_ACTION_UNION_FIELD, ast_action_union_field_action);
    REGISTER(AST_ACTION_UNION_FIELD_LIST, ast_action_union_field_list_action);
    REGISTER(AST_ACTION_BIT_FIELD_FIELD, ast_action_bit_field_field_action);
    REGISTER(AST_ACTION_BIT_FIELD_FIELD_LIST, ast_action_bit_field_field_list_action);
    REGISTER(AST_ACTION_STRUCT_FIELD, ast_action_struct_field_action);
    REGISTER(AST_ACTION_STRUCT_FIELD_LIST, ast_action_struct_field_list_action);
    REGISTER(AST_ACTION_AUTO_CAST_EXPR, ast_action_auto_cast_expr_action);
    REGISTER(AST_ACTION_CAST_EXPR, ast_action_cast_expr_action);
    REGISTER(AST_ACTION_TRANSMUTE_EXPR, ast_action_transmute_expr_action);
    REGISTER(AST_ACTION_LEN_EXPR, ast_action_len_expr_action);
    REGISTER(AST_ACTION_CAP_EXPR, ast_action_cap_expr_action);
    REGISTER(AST_ACTION_SIZE_OF_EXPR, ast_action_size_of_expr_action);
    REGISTER(AST_ACTION_ALIGN_OF_EXPR, ast_action_align_of_expr_action);
    REGISTER(AST_ACTION_OFFSET_OF_EXPR, ast_action_offset_of_expr_action);
    REGISTER(AST_ACTION_RAW_DATA_EXPR, ast_action_raw_data_expr_action);
    REGISTER(AST_ACTION_MIN_EXPR, ast_action_min_expr_action);
    REGISTER(AST_ACTION_MAX_EXPR, ast_action_max_expr_action);
    REGISTER(AST_ACTION_TYPE_OF_EXPR, ast_action_type_of_expr_action);
    REGISTER(AST_ACTION_MAKE_EXPR, ast_action_make_expr_action);
    REGISTER(AST_ACTION_NEW_EXPR, ast_action_new_expr_action);
    REGISTER(AST_ACTION_DELETE_EXPR, ast_action_delete_expr_action);
    REGISTER(AST_ACTION_INCL_EXPR, ast_action_incl_expr_action);
    REGISTER(AST_ACTION_EXCL_EXPR, ast_action_excl_expr_action);
    REGISTER(AST_ACTION_COMPLEX_EXPR, ast_action_complex_expr_action);
    REGISTER(AST_ACTION_QUATERNION_EXPR, ast_action_quaternion_expr_action);
    REGISTER(AST_ACTION_MAYBE_TYPE, ast_action_maybe_type_action);

#undef REGISTER
}

void
odin_grammar_node_free(void * node, void * user_data)
{
    (void)user_data;
    if (node == NULL)
        return;

    odin_grammar_node_t * n = (odin_grammar_node_t *)node;
    for (size_t i = 0; i < n->list.count; i++)
    {
        odin_grammar_node_free(n->list.children[i], NULL);
    }
    free(n->list.children);
    free((char *)n->text);
    free(n->metadata);
    free(n);
}
