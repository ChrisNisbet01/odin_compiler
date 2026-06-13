#include "odin_grammar.h"
#include "odin_grammar_ast.h"
#include "odin_grammar_ast_actions.h"

#include <easy_pc/easy_pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
read_file(char const * path, long * out_len)
{
    FILE * f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char * buf = malloc((size_t)len + 1);
    if (buf == NULL)
    {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[read] = '\0';
    *out_len = (long)read;
    return buf;
}

int
main(int argc, char * argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <input.odin> [--cpt]\n", argv[0]);
        fprintf(stderr, "  --cpt    Print the Concrete Parse Tree\n");
        return EXIT_FAILURE;
    }

    char const * filename = argv[1];
    bool print_cpt = false;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--cpt") == 0)
        {
            print_cpt = true;
        }
    }

    long src_len;
    char * src = read_file(filename, &src_len);
    if (src == NULL)
    {
        return EXIT_FAILURE;
    }

    epc_parser_list * list = epc_parser_list_create();
    if (list == NULL)
    {
        fprintf(stderr, "Error: Failed to create parser list.\n");
        free(src);
        return EXIT_FAILURE;
    }

    epc_parser_t * parser = create_odin_grammar_parser(list);
    if (parser == NULL)
    {
        fprintf(stderr, "Error: Failed to create Odin parser.\n");
        epc_parser_list_free(list);
        free(src);
        return EXIT_FAILURE;
    }

    epc_parse_session_t session = epc_parse_str(parser, src, NULL);

    if (session.result.is_error)
    {
        epc_parser_error_t * err = session.result.data.error;
        fprintf(stderr, "Parse Error: %s\n", err->message);
        fprintf(stderr, "At line %zu, col %zu\n", err->view.line_number, err->view.column_number);
        fprintf(stderr, "Expected: %s\n", err->expected);
        fprintf(stderr, "Found: %s\n", err->found);
        epc_parse_session_destroy(&session);
        epc_parser_list_free(list);
        free(src);
        return EXIT_FAILURE;
    }

    printf("Parse successful!\n");

    if (print_cpt)
    {
        char * cpt_str = epc_cpt_to_string(session.internal_parse_ctx, session.result.data.success);
        if (cpt_str != NULL)
        {
            printf("Concrete Parse Tree:\n%s\n", cpt_str);
            free(cpt_str);
        }
    }

    // Build the AST
    epc_ast_hook_registry_t * registry = epc_ast_hook_registry_create(ODIN_GRAMMAR_AST_ACTION_COUNT__);
    if (registry != NULL)
    {
        odin_grammar_ast_hook_registry_init(registry);

        epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, registry, NULL);
        if (!ast_result.has_error)
        {
            printf("AST build successful!\n");

            odin_grammar_node_t * ast_root = (odin_grammar_node_t *)ast_result.ast_root;
            if (ast_root != NULL)
            {
                printf("AST root type: %d, children: %zu\n",
                       ast_root->type, ast_root->list.count);
            }

            odin_grammar_node_free(ast_root, NULL);
        }
        else
        {
            fprintf(stderr, "AST Build Error: %s\n", ast_result.error_message);
        }

        epc_ast_hook_registry_free(registry);
    }

    epc_parse_session_destroy(&session);
    epc_parser_list_free(list);
    free(src);

    return EXIT_SUCCESS;
}
