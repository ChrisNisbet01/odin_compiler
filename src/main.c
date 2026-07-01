#include "ast_node_name.h"
#include "generator_lists.h"
#include "ir_gen_error.h"
#include "llvm_ir_generator.h"
#include "odin_grammar.h"
#include "odin_grammar_ast.h"
#include "odin_grammar_ast_actions.h"
#include "package_resolver.h"
#include "semantic_analyser.h"
#include "type_descriptors.h"

#include <easy_pc/easy_pc.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "0.1.0"

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

static void
print_usage(char const * prog)
{
    printf("Usage:\n");
    printf("  %s build <file>              Parse, type-check, and compile\n", prog);
    printf("  %s check <file>              Parse and type-check only\n", prog);
    printf("  %s version                   Print version\n", prog);
    printf("  %s help                      Print this help\n", prog);
}

int
main(int argc, char * argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char const * command = argv[1];

    if (strcmp(command, "version") == 0)
    {
        printf("odinc version %s\n", VERSION);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "help") == 0)
    {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (strcmp(command, "build") != 0 && strcmp(command, "check") != 0)
    {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    bool do_codegen = (strcmp(command, "build") == 0);

    if (argc < 3)
    {
        fprintf(stderr, "Error: Missing input file\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char const * filename = argv[2];

    long src_len;
    char * src = read_file(filename, &src_len);
    if (src == NULL)
        return EXIT_FAILURE;

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

    // Build AST
    epc_ast_hook_registry_t * hook_registry = epc_ast_hook_registry_create(ODIN_GRAMMAR_AST_ACTION_COUNT__);
    if (hook_registry == NULL)
    {
        fprintf(stderr, "Error: Failed to create hook registry.\n");
        epc_parse_session_destroy(&session);
        epc_parser_list_free(list);
        free(src);
        return EXIT_FAILURE;
    }

    odin_grammar_ast_hook_registry_init(hook_registry);

    epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, hook_registry, NULL);
    if (ast_result.has_error)
    {
        fprintf(stderr, "AST Build Error: %s\n", ast_result.error_message);
        epc_ast_hook_registry_free(hook_registry);
        epc_parse_session_destroy(&session);
        epc_parser_list_free(list);
        free(src);
        return EXIT_FAILURE;
    }

    printf("AST build successful!\n");

    odin_grammar_node_t * ast_root = (odin_grammar_node_t *)ast_result.ast_root;

    // Resolve ODIN_ROOT
    char * odin_root = resolve_odin_root(argv[0]);
    if (odin_root != NULL)
    {
        printf("ODIN_ROOT: %s\n", odin_root);
    }

    // Compute source directory from filename
    char * filename_copy = strdup(filename);
    char * dir = dirname(filename_copy);
    char * source_dir = strdup(dir);
    free(filename_copy);

    // Semantic analysis
    LLVMContextRef llvm_ctx = LLVMContextCreate();
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(llvm_ctx);
    LLVMTargetDataRef data_layout = NULL; // Will be set by IR gen context

    TypeDescriptors * type_reg = type_descriptors_create_registry(llvm_ctx, data_layout, builder);
    if (type_reg == NULL)
    {
        fprintf(stderr, "Error: Failed to create type registry.\n");
        free(source_dir);
        free(odin_root);
        odin_grammar_node_free(ast_root, NULL);
        epc_ast_hook_registry_free(hook_registry);
        epc_parse_session_destroy(&session);
        epc_parser_list_free(list);
        LLVMDisposeBuilder(builder);
        LLVMContextDispose(llvm_ctx);
        free(src);
        return EXIT_FAILURE;
    }

    GeneratorContext * gen_ctx = generator_context_create(llvm_ctx, builder, type_reg);
    if (gen_ctx == NULL)
    {
        fprintf(stderr, "Error: Failed to create generator context.\n");
        free(source_dir);
        free(odin_root);
        type_descriptors_destroy_registry(type_reg);
        odin_grammar_node_free(ast_root, NULL);
        epc_ast_hook_registry_free(hook_registry);
        epc_parse_session_destroy(&session);
        epc_parser_list_free(list);
        LLVMDisposeBuilder(builder);
        LLVMContextDispose(llvm_ctx);
        free(src);
        return EXIT_FAILURE;
    }

    SemContext sem_ctx;
    sem_context_init(&sem_ctx, ast_root, type_reg, gen_ctx, filename, source_dir, odin_root, parser, hook_registry);

    bool sem_ok = sem_analyse(&sem_ctx);

    if (sem_error_list_has_errors(&sem_ctx.errors))
    {
        fprintf(stderr, "Semantic analysis failed:\n");
        sem_error_list_print(&sem_ctx.errors);
        sem_error_list_init(&sem_ctx.errors);
    }

    if (do_codegen && sem_ok)
    {
        IrGenContext * ir_ctx = ir_gen_context_create("main", type_reg, gen_ctx);
        ir_ctx->imports = sem_ctx.imports;
        ir_ctx->import_count = sem_ctx.import_count;
        if (ir_ctx == NULL)
        {
            fprintf(stderr, "Error: Failed to create IR generator context.\n");
            sem_context_destroy(&sem_ctx);
            type_descriptors_destroy_registry(type_reg);
            generator_context_destroy(gen_ctx);
            odin_grammar_node_free(ast_root, NULL);
            epc_ast_hook_registry_free(hook_registry);
            epc_parse_session_destroy(&session);
            epc_parser_list_free(list);
            LLVMDisposeBuilder(builder);
            LLVMContextDispose(llvm_ctx);
            free(source_dir);
            free(odin_root);
            free(src);
            return EXIT_FAILURE;
        }

        bool ir_ok = ir_generate(ir_ctx, ast_root);

        if (ir_ok)
        {
            char out_name[256];
            snprintf(out_name, sizeof(out_name), "%s.ll", filename);
            if (write_llvm_ir_to_file(ir_ctx->module, out_name) == 0)
            {
                printf("Generated IR: %s\n", out_name);
            }
            else
            {
                fprintf(stderr, "Error: Failed to write IR to '%s'\n", out_name);
            }
        }
        else
        {
            fprintf(stderr, "IR generation failed.\n");
            ir_gen_error_collection_print(&ir_ctx->errors);
        }

        ir_gen_context_destroy(ir_ctx);
    }

    // Cleanup
    sem_context_destroy(&sem_ctx);
    type_descriptors_destroy_registry(type_reg);
    generator_context_destroy(gen_ctx);
    odin_grammar_node_free(ast_root, NULL);
    epc_ast_hook_registry_free(hook_registry);
    epc_parse_session_destroy(&session);
    epc_parser_list_free(list);
    LLVMDisposeBuilder(builder);
    LLVMContextDispose(llvm_ctx);
    free(source_dir);
    free(odin_root);
    free(src);

    return sem_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
