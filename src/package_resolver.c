#include "package_resolver.h"

#include "odin_grammar.h"
#include "odin_grammar_ast_actions.h"
#include "scope.h"

#include <easy_pc/easy_pc.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// --- ODIN_ROOT resolution ---

char *
resolve_odin_root(char const * exe_path)
{
    // 1. Check ODIN_ROOT environment variable
    char const * env_root = getenv("ODIN_ROOT");
    if (env_root != NULL && env_root[0] != '\0')
    {
        if (env_root[0] == '/')
            return strdup(env_root);
        char * cwd = getcwd(NULL, 0);
        if (cwd == NULL)
            return strdup(env_root);
        size_t needed = strlen(cwd) + 1 + strlen(env_root) + 1;
        char * abs_path = malloc(needed);
        snprintf(abs_path, needed, "%s/%s", cwd, env_root);
        free(cwd);
        return abs_path;
    }

    // 2. Default: <exe_dir>/../..
    // Resolve exe path to real path first (handles symlinks, relative paths)
    char * real_exe = realpath(exe_path, NULL);
    char const * exe_to_use = real_exe != NULL ? real_exe : exe_path;

    char * exe_copy = strdup(exe_to_use);
    char * exe_dir = dirname(exe_copy);

    char const * rel_path = "../..";
    size_t needed = strlen(exe_dir) + 1 + strlen(rel_path) + 1;
    char * combined = malloc(needed);
    snprintf(combined, needed, "%s/%s", exe_dir, rel_path);

    free(exe_copy);
    free(real_exe);

    char * resolved = realpath(combined, NULL);
    if (resolved == NULL)
        resolved = combined;
    else
        free(combined);

    return resolved;
}

// --- Import path resolution ---

static bool
file_exists(char const * path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

char *
resolve_import_path(char const * import_name, char const * source_dir, char const * odin_root)
{
    if (import_name == NULL || import_name[0] == '\0')
        return NULL;

    // 1. Check <source_dir>/<import_name>/<import_name>.odin
    size_t needed = strlen(source_dir) + 1 + strlen(import_name) + 1 + strlen(import_name) + 6;
    char * candidate = malloc(needed);
    snprintf(candidate, needed, "%s/%s/%s.odin", source_dir, import_name, import_name);
    if (file_exists(candidate))
        return candidate;
    free(candidate);

    // 2. Check <source_dir>/<import_name>.odin
    needed = strlen(source_dir) + 1 + strlen(import_name) + 6;
    candidate = malloc(needed);
    snprintf(candidate, needed, "%s/%s.odin", source_dir, import_name);
    if (file_exists(candidate))
        return candidate;
    free(candidate);

    // 3. Check <odin_root>/src/<import_name>/<import_name>.odin
    if (odin_root != NULL)
    {
        needed = strlen(odin_root) + 5 + strlen(import_name) + 1 + strlen(import_name) + 6;
        candidate = malloc(needed);
        snprintf(candidate, needed, "%s/src/%s/%s.odin", odin_root, import_name, import_name);
        if (file_exists(candidate))
            return candidate;
        free(candidate);

        // 4. Check <odin_root>/src/<import_name>.odin
        needed = strlen(odin_root) + 5 + strlen(import_name) + 6;
        candidate = malloc(needed);
        snprintf(candidate, needed, "%s/src/%s.odin", odin_root, import_name);
        if (file_exists(candidate))
            return candidate;
        free(candidate);
    }

    return NULL;
}

// --- File parsing ---

ImportedPackage *
parse_imported_file(char const * file_path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks)
{
    if (file_path == NULL || parser == NULL || hooks == NULL)
        return NULL;

    // Read the file
    FILE * f = fopen(file_path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: Cannot open imported file '%s'\n", file_path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char * src = malloc((size_t)len + 1);
    if (src == NULL)
    {
        fclose(f);
        return NULL;
    }

    size_t read = fread(src, 1, (size_t)len, f);
    fclose(f);
    src[read] = '\0';

    // Parse
    epc_parse_session_t session = epc_parse_str(parser, src, NULL);
    if (session.result.is_error)
    {
        epc_parser_error_t * err = session.result.data.error;
        fprintf(
            stderr,
            "Parse Error in imported file '%s': %s\nAt line %zu, col %zu\nExpected: %s\nFound: %s\n",
            file_path,
            err->message,
            err->view.line_number,
            err->view.column_number,
            err->expected,
            err->found
        );
        epc_parse_session_destroy(&session);
        free(src);
        return NULL;
    }

    // Build AST
    epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, hooks, NULL);
    if (ast_result.has_error)
    {
        fprintf(stderr, "AST Build Error in imported file '%s': %s\n", file_path, ast_result.error_message);
        epc_parse_session_destroy(&session);
        free(src);
        return NULL;
    }

    // Session is no longer needed (AST nodes have their own copies of text)
    epc_parse_session_destroy(&session);

    // Extract source directory from file path
    char * path_copy = strdup(file_path);
    char * dir = dirname(path_copy);
    char * source_dir = strdup(dir);
    free(path_copy);

    // Allocate and populate the package
    ImportedPackage * pkg = calloc(1, sizeof(ImportedPackage));
    if (pkg == NULL)
    {
        free(source_dir);
        odin_grammar_node_free(ast_result.ast_root, NULL);
        free(src);
        return NULL;
    }

    pkg->source_path = strdup(file_path);
    pkg->source_dir = source_dir;
    pkg->source_text = src;
    pkg->ast = (odin_grammar_node_t *)ast_result.ast_root;
    pkg->analysed = false;

    // Extract package name from the AST (PackageClause is inside ExternalDeclarations)
    if (pkg->ast != NULL)
    {
        for (size_t i = 0; i < pkg->ast->list.count; i++)
        {
            odin_grammar_node_t * ext = pkg->ast->list.children[i];
            if (ext && ext->type == AST_NODE_EXTERNAL_DECLARATIONS)
            {
                for (size_t j = 0; j < ext->list.count; j++)
                {
                    odin_grammar_node_t * child = ext->list.children[j];
                    if (child && child->type == AST_NODE_PACKAGE_CLAUSE)
                    {
                        // PackageClause children: [Identifier("package_name")]
                        if (child->list.count > 0 && child->list.children[0] && child->list.children[0]->text)
                        {
                            pkg->package_name = strdup(child->list.children[0]->text);
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    return pkg;
}

void
imported_package_free(ImportedPackage * pkg)
{
    if (pkg == NULL)
        return;
    free(pkg->source_path);
    free(pkg->source_dir);
    free(pkg->source_text);
    free(pkg->package_name);
    if (pkg->package_scope != NULL)
        scope_free(pkg->package_scope);
    if (pkg->ast != NULL)
        odin_grammar_node_free(pkg->ast, NULL);
    free(pkg);
}
