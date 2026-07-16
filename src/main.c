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
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define VERSION "0.1.0"

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
    printf("  %s build [-o <output>] [--keep-temps] [--file <file>] <path>\n", prog);
    printf("                           Parse, type-check, compile, and link\n");
    printf("                           If <path> is a directory, all .odin files in it\n");
    printf("                           are compiled as a single package.\n");
    printf("                           Use --file <file> to force single-file mode.\n");
    printf("  %s run [options] <path>  Compile, link, and run\n", prog);
    printf("  %s check [options] <path> Parse and type-check only\n", prog);
    printf("  %s version               Print version\n", prog);
    printf("  %s help                  Print this help\n", prog);
}

// --- Directory enumeration ---

static char **
enumerate_odin_files(char const * dir_path, int * out_count)
{
    DIR * d = opendir(dir_path);
    if (d == NULL)
    {
        fprintf(stderr, "Error: Cannot open directory '%s'\n", dir_path);
        *out_count = 0;
        return NULL;
    }

    int capacity = 16;
    int count = 0;
    char ** files = malloc((size_t)capacity * sizeof(char *));

    struct dirent * entry;
    while ((entry = readdir(d)) != NULL)
    {
        char const * name = entry->d_name;
        size_t namelen = strlen(name);
        if (namelen < 6 || strcmp(name + namelen - 5, ".odin") != 0)
            continue;

        if (count >= capacity)
        {
            capacity *= 2;
            files = realloc(files, (size_t)capacity * sizeof(char *));
        }

        size_t pathlen = strlen(dir_path) + 1 + namelen + 1;
        files[count] = malloc(pathlen);
        snprintf(files[count], pathlen, "%s/%s", dir_path, name);
        count++;
    }

    closedir(d);

    if (count == 0)
    {
        fprintf(stderr, "Error: No .odin files found in '%s'\n", dir_path);
        free(files);
        *out_count = 0;
        return NULL;
    }

    *out_count = count;
    return files;
}

// --- Parallel parsing ---

typedef struct
{
    char const * file_path;
    epc_parser_t * parser;
    epc_ast_hook_registry_t * hooks;
    ParsedFile * result;
    char * error;
} ParseJob;

static void *
parse_job_thread(void * arg)
{
    ParseJob * job = (ParseJob *)arg;
    job->result = parse_source_file(job->file_path, job->parser, job->hooks);
    return NULL;
}

// --- Main ---

typedef struct
{
    SemContext * sem_ctx;
    TypeDescriptors * type_reg;
    GeneratorContext * gen_ctx;
    odin_grammar_node_t * ast_root;
    epc_ast_hook_registry_t * hook_registry;
    epc_parser_list * parser_list;
    LLVMBuilderRef builder;
    LLVMContextRef llvm_ctx;
    char * source_dir;
    char * odin_root;
    char * ll_file;
    char * exe_file;
} CompilerResources;

static void
resources_free(CompilerResources * r)
{
    if (r->sem_ctx)
        sem_context_destroy(r->sem_ctx);
    if (r->type_reg)
        type_descriptors_destroy_registry(r->type_reg);
    if (r->gen_ctx)
        generator_context_destroy(r->gen_ctx);
    if (r->ast_root)
        odin_grammar_node_free(r->ast_root, NULL);
    if (r->hook_registry)
        epc_ast_hook_registry_free(r->hook_registry);
    if (r->parser_list)
        epc_parser_list_free(r->parser_list);
    if (r->builder)
        LLVMDisposeBuilder(r->builder);
    if (r->llvm_ctx)
        LLVMContextDispose(r->llvm_ctx);
    free(r->source_dir);
    free(r->odin_root);
    free(r->ll_file);
    free(r->exe_file);
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

    CompilerResources r = {0};
    int exit_code = EXIT_FAILURE;

    // Parse options and extract path
    char const * filename = NULL;
    char const * output_name = NULL;
    bool keep_temps = false;
    bool force_single_file = false;

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
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
        {
            force_single_file = true;
            filename = argv[++i];
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
        fprintf(stderr, "Error: Missing input path\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Create parser infrastructure (shared across all files)
    epc_parser_list * list = epc_parser_list_create();
    if (list == NULL)
    {
        fprintf(stderr, "Error: Failed to create parser list.\n");
        return EXIT_FAILURE;
    }
    r.parser_list = list;

    epc_parser_t * base_parser = create_odin_grammar_parser(list);
    if (base_parser == NULL)
    {
        fprintf(stderr, "Error: Failed to create Odin parser.\n");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }

    epc_ast_hook_registry_t * hook_registry = epc_ast_hook_registry_create(ODIN_GRAMMAR_AST_ACTION_COUNT__);
    if (hook_registry == NULL)
    {
        fprintf(stderr, "Error: Failed to create hook registry.\n");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }
    r.hook_registry = hook_registry;
    odin_grammar_ast_hook_registry_init(hook_registry);

    // Determine if input is a directory or single file
    bool is_dir = false;
    if (!force_single_file)
    {
        struct stat st;
        if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode))
            is_dir = true;
    }

    odin_grammar_node_t * ast_root = NULL;
    char * source_dir = NULL;
    ParsedFile ** parsed_files = NULL;

    if (is_dir)
    {
        // --- Directory mode ---
        printf("Compiling directory: %s\n", filename);

        int file_count = 0;
        char ** odin_files = enumerate_odin_files(filename, &file_count);
        if (odin_files == NULL)
        {
            resources_free(&r);
            return EXIT_FAILURE;
        }

        // Create parse jobs
        ParseJob * jobs = calloc((size_t)file_count, sizeof(ParseJob));
        pthread_t * threads = calloc((size_t)file_count, sizeof(pthread_t));

        for (int i = 0; i < file_count; i++)
        {
            jobs[i].file_path = odin_files[i];
            jobs[i].hooks = hook_registry;
            // Each thread needs its own parser (concurrent parse_str)
            epc_parser_list * thr_list = epc_parser_list_create();
            if (thr_list)
                jobs[i].parser = create_odin_grammar_parser(thr_list);
            if (jobs[i].parser == NULL)
            {
                fprintf(stderr, "Error: Failed to create thread parser.\n");
                jobs[i].result = NULL;
            }
            else
            {
                pthread_create(&threads[i], NULL, parse_job_thread, &jobs[i]);
            }
        }

        // Wait for all parse jobs
        bool parse_ok = true;
        for (int i = 0; i < file_count; i++)
        {
            pthread_join(threads[i], NULL);
            if (jobs[i].result == NULL)
            {
                fprintf(stderr, "Error: Failed to parse '%s'\n", jobs[i].file_path);
                parse_ok = false;
            }
        }

        if (!parse_ok)
        {
            // Clean up partial results
            for (int i = 0; i < file_count; i++)
            {
                if (jobs[i].result)
                    parsed_file_free(jobs[i].result);
                free((char *)odin_files[i]);
            }
            free(odin_files);
            free(jobs);
            free(threads);
            resources_free(&r);
            return EXIT_FAILURE;
        }

        // Collect results
        parsed_files = calloc((size_t)file_count, sizeof(ParsedFile *));
        for (int i = 0; i < file_count; i++)
        {
            parsed_files[i] = jobs[i].result;
        }

        // Merge ASTs
        char * merge_error = NULL;
        ast_root = merge_program_asts(parsed_files, file_count, &merge_error);
        if (ast_root == NULL)
        {
            fprintf(stderr, "Error: %s\n", merge_error ? merge_error : "Failed to merge ASTs");
            free(merge_error);
            for (int i = 0; i < file_count; i++)
            {
                parsed_file_free(parsed_files[i]);
                free((char *)odin_files[i]);
            }
            free(parsed_files);
            free(odin_files);
            free(jobs);
            free(threads);
            resources_free(&r);
            return EXIT_FAILURE;
        }

        // Set source directory
        source_dir = strdup(filename);

        // Clean up per-file parsers and file listing
        for (int i = 0; i < file_count; i++)
        {
            parsed_file_free(parsed_files[i]); // AST children moved to merged tree
            free((char *)odin_files[i]);
        }
        free(parsed_files);
        parsed_files = NULL;
        free(odin_files);
        free(jobs);
        free(threads);

        printf("Parsed %d files, merged into single AST\n", file_count);
    }
    else
    {
        // --- Single file mode ---
        ParsedFile * pf = parse_source_file(filename, base_parser, hook_registry);
        if (pf == NULL)
        {
            resources_free(&r);
            return EXIT_FAILURE;
        }
        ast_root = pf->ast;
        pf->ast = NULL; // transfer ownership
        source_dir = strdup(filename);
        // Extract directory from filename for source_dir
        char * fn_copy = strdup(filename);
        char * dir = dirname(fn_copy);
        free(source_dir);
        source_dir = strdup(dir);
        free(fn_copy);
        parsed_file_free(pf);

        printf("Parse successful!\n");
    }

    r.ast_root = ast_root;
    r.source_dir = source_dir;

    // Resolve ODIN_ROOT
    char * odin_root = resolve_odin_root(argv[0]);
    r.odin_root = odin_root;
    if (odin_root != NULL)
        printf("ODIN_ROOT: %s\n", odin_root);

    // Semantic analysis
    LLVMContextRef llvm_ctx = LLVMContextCreate();
    r.llvm_ctx = llvm_ctx;
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(llvm_ctx);
    r.builder = builder;
    LLVMTargetDataRef data_layout = NULL;

    TypeDescriptors * type_reg = type_descriptors_create_registry(llvm_ctx, data_layout, builder);
    if (type_reg == NULL)
    {
        fprintf(stderr, "Error: Failed to create type registry.\n");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }
    r.type_reg = type_reg;

    GeneratorContext * gen_ctx = generator_context_create(llvm_ctx, builder, type_reg);
    if (gen_ctx == NULL)
    {
        fprintf(stderr, "Error: Failed to create generator context.\n");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }
    r.gen_ctx = gen_ctx;

    SemContext sem_ctx;
    sem_context_init(&sem_ctx, ast_root, type_reg, gen_ctx, filename, source_dir, odin_root, base_parser, hook_registry);
    r.sem_ctx = &sem_ctx;

    bool sem_ok = sem_analyse(&sem_ctx);

    if (sem_error_list_has_errors(&sem_ctx.errors))
    {
        sem_error_list_print(&sem_ctx.errors);
        sem_error_list_init(&sem_ctx.errors);
    }

    // Compute output filenames
    char * ll_file = NULL;
    char * exe_file = NULL;

    // Determine base name for output: dir basename (if directory) or file basename (if single file)
    char const * output_base = NULL;
    char * output_base_buf = NULL;
    if (is_dir)
    {
        // Use <dir_path>/<dir_basename> as output base so outputs go alongside source files
        size_t dir_len = strlen(filename);
        while (dir_len > 1 && filename[dir_len - 1] == '/')
            dir_len--;
        char * dir_stripped = strndup(filename, dir_len);
        char const * last_slash = strrchr(dir_stripped, '/');
        char const * base_name = (last_slash != NULL) ? last_slash + 1 : dir_stripped;
        size_t out_len = dir_len + 1 + strlen(base_name);
        output_base_buf = malloc(out_len + 1);
        snprintf(output_base_buf, out_len + 1, "%.*s/%s", (int)dir_len, filename, base_name);
        output_base = output_base_buf;
        free(dir_stripped);
    }
    else
    {
        output_base = filename;
    }

    if (strcmp(command, "run") == 0)
    {
        char const * base = strrchr(output_base, '/');
        base = (base != NULL) ? base + 1 : output_base;
        size_t base_len = strlen(base);
        if (base_len > 5 && strcmp(base + base_len - 5, ".odin") == 0)
            base_len -= 5;
        ll_file = malloc(base_len + 20);
        snprintf(ll_file, base_len + 20, "/tmp/odinc_%.*s", (int)base_len, base);
        exe_file = strdup(ll_file);
        size_t ll_len = strlen(ll_file);
        char * ll_ext = realloc(ll_file, ll_len + 4);
        if (ll_ext == NULL) { free(ll_file); free(exe_file); free(output_base_buf); return EXIT_FAILURE; }
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
        char const * dot = strrchr(output_base, '.');
        size_t base_len = (dot != NULL) ? (size_t)(dot - output_base) : strlen(output_base);
        exe_file = malloc(base_len + 1);
        memcpy(exe_file, output_base, base_len);
        exe_file[base_len] = '\0';
        ll_file = malloc(base_len + 4);
        snprintf(ll_file, base_len + 4, "%s.ll", exe_file);
    }

    free(output_base_buf);
    output_base_buf = NULL;

    r.ll_file = ll_file;
    r.exe_file = exe_file;

    if (do_codegen && sem_ok)
    {
        IrGenContext * ir_ctx = ir_gen_context_create("main", type_reg, gen_ctx);
        ir_ctx->imports = sem_ctx.imports;
        ir_ctx->import_count = sem_ctx.import_count;
        // IR gen uses node->file_path for errors now, so we can pass NULL
        ir_ctx->file_path = NULL;
        if (ir_ctx == NULL)
        {
            fprintf(stderr, "Error: Failed to create IR generator context.\n");
            resources_free(&r);
            return EXIT_FAILURE;
        }

        bool ir_ok = ir_generate(ir_ctx, ast_root);

        if (ir_ok)
        {
            if (write_llvm_ir_to_file(ir_ctx->module, ll_file) == 0)
            {
                printf("Generated IR: %s\n", ll_file);
            }
            else
            {
                fprintf(stderr, "Error: Failed to write IR to '%s'\n", ll_file);
            }

            if (run_linker(ll_file, exe_file, ir_ctx) == 0)
            {
                printf("Generated executable: %s\n", exe_file);

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

            if (!keep_temps)
                remove(ll_file);
        }
        else
        {
            ir_gen_error_collection_print(&ir_ctx->errors);
        }

        ir_gen_context_destroy(ir_ctx);
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    resources_free(&r);

    return exit_code;
}
