#include "sem_context.h"

#include "ast_utils.h"
#include "scope.h"
#include "symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

calling_convention_t
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

bool
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

char *
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

ImportedPackage *
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

void
sem_collect_comma_chain_args(odin_grammar_node_t * node, odin_grammar_node_t ** out_args, int max_args, int * out_count)
{
    if (node == NULL || max_args <= 0)
        return;
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
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
        if (*out_count < max_args)
        {
            out_args[*out_count] = node;
            (*out_count)++;
        }
    }
}
