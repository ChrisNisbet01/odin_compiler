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
#include <unistd.h>
#include <sys/wait.h>

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

static int
run_linker(char const * ll_file, char const * output_file, IrGenContext * ir_ctx)
{
    char const * cc = getenv("CC");
    if (cc == NULL || cc[0] == '\0')
        cc = "clang";

    size_t cmd_len = strlen(cc) + strlen(ll_file) + strlen(output_file) + 64;
    for (int i = 0; i < ir_ctx->foreign_library_count; i++)
    {
        cmd_len += strlen(ir_ctx->foreign_libraries[i]) + 6;
    }
    cmd_len += 8;

    char * cmd = malloc(cmd_len);
    size_t pos = snprintf(cmd, cmd_len, "%s '%s' -o '%s'", cc, ll_file, output_file);

    for (int i = 0; i < ir_ctx->foreign_library_count; i++)
    {
        pos += snprintf(cmd + pos, cmd_len - pos, " -l%s", ir_ctx->foreign_libraries[i]);
    }
    pos += snprintf(cmd + pos, cmd_len - pos, " -lm");

    printf("  [LINK] %s\n", cmd);
    int ret = system(cmd);
    free(cmd);
    return ret;
}

static void
print_usage(char const * prog)
{
    printf("Usage:\n");
    printf("  %s build [-o <output>] [--keep-temps] <file>\n", prog);
    printf("                           Parse, type-check, compile, and link\n");
    printf("  %s run [options] <file> Compile, link, and run\n", prog);
    printf("  %s check <file>         Parse and type-check only\n", prog);
    printf("  %s version              Print version\n", prog);
    printf("  %s help                 Print this help\n", prog);
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

    if (strcmp(command, "build") != 0 && strcmp(command, "check") != 0 && strcmp(command, "run") != 0)
    {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    bool do_codegen = (strcmp(command, "build") == 0 || strcmp(command, "run") == 0);

    // Parse options and extract filename
    char const * filename = NULL;
    char const * output_name = NULL;
    bool keep_temps = false;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            output_name = argv[++i];
        }
        else if (strcmp(argv[i], "--keep-temps") == 0)
        {
            keep_temps = true;
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        else
        {
            filename = argv[i];
        }
    }

    if (filename == NULL)
    {
        fprintf(stderr, "Error: Missing input file\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

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
        sem_error_list_print(&sem_ctx.errors);
        sem_error_list_init(&sem_ctx.errors);
    }

    // Compute output filenames
    char * ll_file = NULL;
    char * exe_file = NULL;

    if (strcmp(command, "run") == 0)
    {
        // Use temp file for run command
        char const * base = strrchr(filename, '/');
        base = (base != NULL) ? base + 1 : filename;
        size_t base_len = strlen(base);
        // Strip .odin extension if present
        if (base_len > 5 && strcmp(base + base_len - 5, ".odin") == 0)
            base_len -= 5;
        ll_file = malloc(base_len + 20);
        snprintf(ll_file, base_len + 20, "/tmp/odinc_%.*s", (int)base_len, base);
        exe_file = strdup(ll_file);
        // Add .ll extension
        size_t ll_len = strlen(ll_file);
        char * ll_ext = realloc(ll_file, ll_len + 4);
        if (ll_ext == NULL) { free(ll_file); free(exe_file); return EXIT_FAILURE; }
        ll_file = ll_ext;
        memcpy(ll_file + ll_len, ".ll", 4);
    }
    else if (output_name != NULL)
    {
        size_t out_len = strlen(output_name);
        ll_file = malloc(out_len + 4);
        exe_file = strdup(output_name);
        snprintf(ll_file, out_len + 4, "%s.ll", output_name);
    }
    else
    {
        char const * dot = strrchr(filename, '.');
        size_t base_len = (dot != NULL) ? (size_t)(dot - filename) : strlen(filename);
        exe_file = malloc(base_len + 1);
        memcpy(exe_file, filename, base_len);
        exe_file[base_len] = '\0';
        ll_file = malloc(base_len + 4);
        snprintf(ll_file, base_len + 4, "%s.ll", exe_file);
    }

    if (do_codegen && sem_ok)
    {
        IrGenContext * ir_ctx = ir_gen_context_create("main", type_reg, gen_ctx);
        ir_ctx->imports = sem_ctx.imports;
        ir_ctx->import_count = sem_ctx.import_count;
        ir_ctx->file_path = filename;
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
            free(ll_file);
            free(exe_file);
            return EXIT_FAILURE;
        }

        bool ir_ok = ir_generate(ir_ctx, ast_root);

        if (ir_ok)
        {
            // 1. Write .ll file
            if (write_llvm_ir_to_file(ir_ctx->module, ll_file) == 0)
            {
                printf("Generated IR: %s\n", ll_file);
            }
            else
            {
                fprintf(stderr, "Error: Failed to write IR to '%s'\n", ll_file);
            }

            // 2. Compile .ll -> executable using system C compiler
            if (run_linker(ll_file, exe_file, ir_ctx) == 0)
            {
                printf("Generated executable: %s\n", exe_file);

                // odinc run: execute the binary
                if (strcmp(command, "run") == 0)
                {
                    printf("  [RUN] %s\n", exe_file);
                    int run_ret = system(exe_file);
                    int exit_code = WIFEXITED(run_ret) ? WEXITSTATUS(run_ret) : EXIT_FAILURE;
                    printf("  [EXIT] %d\n", exit_code);
                    remove(exe_file);
                    sem_ok = (exit_code == 0);
                }
            }
            else
            {
                fprintf(stderr, "Error: Linking failed for '%s'\n", exe_file);
            }

            // 3. Clean up intermediate files unless --keep-temps
            if (!keep_temps)
            {
                remove(ll_file);
            }
        }
        else
        {
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
    free(ll_file);
    free(exe_file);

    return sem_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
