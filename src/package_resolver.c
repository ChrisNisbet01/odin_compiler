#include "package_resolver.h"

#include "odin_grammar.h"
#include "odin_grammar_ast_actions.h"
#include "scope.h"

#include <easy_pc/easy_pc.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// --- ParsedFile helpers ---

ParsedFile *
parsed_file_create(char const * source_path)
{
    ParsedFile * pf = calloc(1, sizeof(ParsedFile));
    if (pf == NULL) return NULL;
    pf->source_path = source_path ? strdup(source_path) : NULL;
    return pf;
}

void
parsed_file_free(ParsedFile * pf)
{
    if (pf == NULL) return;
    free(pf->source_path);
    free(pf->source_text);
    free(pf->package_name);
    if (pf->ast != NULL)
        odin_grammar_node_free(pf->ast, NULL);
    free(pf);
}

// Recursively set file_path on a node and all its descendants
static void
set_node_file_path(odin_grammar_node_t * node, char const * path)
{
    if (node == NULL) return;
    if (node->file_path == NULL && path != NULL)
        node->file_path = strdup(path);
    for (size_t i = 0; i < node->list.count; i++)
        set_node_file_path(node->list.children[i], path);
}

// --- parse_source_file ---

ParsedFile *
parse_source_file(char const * path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks)
{
    if (path == NULL || parser == NULL || hooks == NULL)
        return NULL;

    // Read the file
    FILE * f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char * src = malloc((size_t)len + 1);
    if (src == NULL) { fclose(f); return NULL; }
    size_t read = fread(src, 1, (size_t)len, f);
    fclose(f);
    src[read] = '\0';

    // Parse
    epc_parse_session_t session = epc_parse_str(parser, src, NULL);
    if (session.result.is_error)
    {
        epc_parser_error_t * err = session.result.data.error;
        fprintf(stderr, "Parse Error in '%s': %s\nAt line %zu, col %zu\nExpected: %s\nFound: %s\n",
                path, err->message, err->view.line_number, err->view.column_number,
                err->expected, err->found);
        epc_parse_session_destroy(&session);
        free(src);
        return NULL;
    }

    // Build AST
    epc_ast_result_t ast_result = epc_ast_build(session.result.data.success, hooks, NULL);
    if (ast_result.has_error)
    {
        fprintf(stderr, "AST Build Error in '%s': %s\n", path, ast_result.error_message);
        epc_parse_session_destroy(&session);
        free(src);
        return NULL;
    }

    epc_parse_session_destroy(&session);

    ParsedFile * pf = parsed_file_create(path);
    if (pf == NULL)
    {
        odin_grammar_node_free(ast_result.ast_root, NULL);
        free(src);
        return NULL;
    }

    pf->source_text = src;
    pf->ast = (odin_grammar_node_t *)ast_result.ast_root;

    // Set file_path on every AST node
    set_node_file_path(pf->ast, path);

    // Extract package name from AST
    odin_grammar_node_t * program = pf->ast;
    for (size_t i = 0; i < program->list.count; i++)
    {
        odin_grammar_node_t * ext = program->list.children[i];
        if (ext && ext->type == AST_NODE_EXTERNAL_DECLARATIONS)
        {
            for (size_t j = 0; j < ext->list.count; j++)
            {
                odin_grammar_node_t * child = ext->list.children[j];
                if (child && child->type == AST_NODE_PACKAGE_CLAUSE)
                {
                    if (child->list.count > 0 && child->list.children[0] && child->list.children[0]->text)
                        pf->package_name = strdup(child->list.children[0]->text);
                    break;
                }
            }
            break;
        }
    }

    return pf;
}

// --- merge_program_asts ---

odin_grammar_node_t *
merge_program_asts(ParsedFile ** files, int file_count, char ** out_error)
{
    if (files == NULL || file_count < 1)
    {
        if (out_error) *out_error = strdup("No files to merge");
        return NULL;
    }

    // Allocate the merged ExternalDeclarations node
    // We'll create it with room for all children across all files (overestimate)
    size_t total_children = 0;
    for (int fi = 0; fi < file_count; fi++)
    {
        if (files[fi] == NULL || files[fi]->ast == NULL) continue;
        for (size_t i = 0; i < files[fi]->ast->list.count; i++)
        {
            odin_grammar_node_t * ext = files[fi]->ast->list.children[i];
            if (ext && ext->type == AST_NODE_EXTERNAL_DECLARATIONS)
                total_children += ext->list.count;
        }
    }

    // Create merged ExternalDeclarations node
    odin_grammar_node_t * merged_ext = calloc(1, sizeof(odin_grammar_node_t));
    if (merged_ext == NULL) { if (out_error) *out_error = strdup("Out of memory"); return NULL; }
    merged_ext->type = AST_NODE_EXTERNAL_DECLARATIONS;
    merged_ext->list.children = calloc(total_children, sizeof(odin_grammar_node_t *));
    if (merged_ext->list.children == NULL)
    {
        free(merged_ext);
        if (out_error) *out_error = strdup("Out of memory");
        return NULL;
    }
    merged_ext->list.count = 0;

    char * seen_pkg_name = NULL;
    bool have_package_clause = false;

    for (int fi = 0; fi < file_count; fi++)
    {
        ParsedFile * pf = files[fi];
        if (pf == NULL || pf->ast == NULL) continue;

        for (size_t i = 0; i < pf->ast->list.count; i++)
        {
            odin_grammar_node_t * ext = pf->ast->list.children[i];
            if (ext == NULL || ext->type != AST_NODE_EXTERNAL_DECLARATIONS)
                continue;

            // Move children from this ExternalDeclarations into the merged node
            for (size_t j = 0; j < ext->list.count; j++)
            {
                odin_grammar_node_t * child = ext->list.children[j];
                if (child == NULL) continue;

                if (child->type == AST_NODE_PACKAGE_CLAUSE)
                {
                    char * pkg_name = (child->list.count > 0 && child->list.children[0])
                                        ? (char *)child->list.children[0]->text : NULL;
                    if (!have_package_clause)
                    {
                        // Take the first package clause
                        merged_ext->list.children[merged_ext->list.count++] = child;
                        if (pkg_name) seen_pkg_name = strdup(pkg_name);
                        have_package_clause = true;
                    }
                    else
                    {
                        // Validate package name matches
                        if (pkg_name && seen_pkg_name && strcmp(pkg_name, seen_pkg_name) != 0)
                        {
                            char err_buf[512];
                            snprintf(err_buf, sizeof(err_buf),
                                     "Package name mismatch: '%s' vs '%s' in '%s'",
                                     pkg_name, seen_pkg_name, pf->source_path ? pf->source_path : "?");
                            if (out_error) *out_error = strdup(err_buf);
                            free(seen_pkg_name);
                            // Don't add this child — just free the merged node later
                            // We need to clean up children already moved
                            // For simplicity, caller handles error
                            odin_grammar_node_free(merged_ext, NULL);
                            return NULL;
                        }
                        // Skip duplicate package clause
                    }
                }
                else if (child->type == AST_NODE_IMPORT || child->type == AST_NODE_IMPORT_NAMED
                      || child->type == AST_NODE_IMPORT_USING)
                {
                    // Add all imports (dedup happens in semantic analysis)
                    merged_ext->list.children[merged_ext->list.count++] = child;
                }
                else
                {
                    merged_ext->list.children[merged_ext->list.count++] = child;
                }
            }

            // Detach children from the source ExternalDeclarations node
            ext->list.count = 0;
            ext->list.children = NULL;
        }
    }

    free(seen_pkg_name);
    seen_pkg_name = NULL;

    // Create the merged Program node
    odin_grammar_node_t * merged_program = calloc(1, sizeof(odin_grammar_node_t));
    if (merged_program == NULL)
    {
        odin_grammar_node_free(merged_ext, NULL);
        if (out_error) *out_error = strdup("Out of memory");
        return NULL;
    }
    merged_program->type = AST_NODE_PROGRAM;
    merged_program->list.children = malloc(sizeof(odin_grammar_node_t *));
    if (merged_program->list.children == NULL)
    {
        free(merged_program);
        odin_grammar_node_free(merged_ext, NULL);
        if (out_error) *out_error = strdup("Out of memory");
        return NULL;
    }
    merged_program->list.children[0] = merged_ext;
    merged_program->list.count = 1;

    return merged_program;
}

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

static char *
try_path(char const * fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) return NULL;

    char * path = malloc((size_t)needed + 1);
    if (path == NULL) return NULL;

    va_start(args, fmt);
    vsnprintf(path, (size_t)needed + 1, fmt, args);
    va_end(args);

    if (file_exists(path))
        return path;
    free(path);
    return NULL;
}

char *
resolve_import_path(char const * import_name, char const * source_dir, char const * odin_root)
{
    if (import_name == NULL || import_name[0] == '\0')
        return NULL;

    // Check for collection prefix: "core:fmt" → collection="core", pkg="fmt"
    char const * colon = strchr(import_name, ':');
    if (colon != NULL)
    {
        size_t coll_len = (size_t)(colon - import_name);
        char const * pkg_name = colon + 1;
        if (pkg_name[0] == '\0')
            return NULL;

        // Determine collection directory base
        // "core" → <odin_root>/core/   (or <odin_root>/ if no odin_root)
        char const * coll_base = odin_root != NULL ? odin_root : source_dir;

        // Try resolution paths:
        //   <coll_base>/<collection>/<pkg>/<pkg>.odin      (ODIN_ROOT = stubs/)
        //   <coll_base>/<collection>/src/<pkg>/<pkg>.odin  (package under src/)
        //   <coll_base>/stubs/<collection>/<pkg>/<pkg>.odin (ODIN_ROOT = project root, stubs layout)
        {
            char * candidate = try_path("%s/%.*s/%s/%s.odin", coll_base, (int)coll_len, import_name, pkg_name, pkg_name);
            if (candidate) return candidate;
        }
        {
            char * candidate = try_path("%s/%.*s/src/%s/%s.odin", coll_base, (int)coll_len, import_name, pkg_name, pkg_name);
            if (candidate) return candidate;
        }
        {
            char * candidate = try_path("%s/stubs/%.*s/%s/%s.odin", coll_base, (int)coll_len, import_name, pkg_name, pkg_name);
            if (candidate) return candidate;
        }
        {
            char * candidate = try_path("%s/stubs/%.*s/src/%s/%s.odin", coll_base, (int)coll_len, import_name, pkg_name, pkg_name);
            if (candidate) return candidate;
        }

        return NULL;
    }

    {
        char * candidate = try_path("%s/%s/%s.odin", source_dir, import_name, import_name);
        if (candidate) return candidate;
    }
    {
        char * candidate = try_path("%s/%s.odin", source_dir, import_name);
        if (candidate) return candidate;
    }
    if (odin_root != NULL)
    {
        {
            char * candidate = try_path("%s/src/%s/%s.odin", odin_root, import_name, import_name);
            if (candidate) return candidate;
        }
        {
            char * candidate = try_path("%s/src/%s.odin", odin_root, import_name);
            if (candidate) return candidate;
        }
    }

    return NULL;
}

// --- File parsing ---

ImportedPackage *
parse_imported_file(char const * file_path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks)
{
    if (file_path == NULL || parser == NULL || hooks == NULL)
        return NULL;

    // Delegate to parse_source_file for the actual parsing
    ParsedFile * pf = parse_source_file(file_path, parser, hooks);
    if (pf == NULL)
        return NULL;

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
        parsed_file_free(pf);
        return NULL;
    }

    // Transfer ownership from ParsedFile to ImportedPackage
    pkg->source_path = pf->source_path;     pf->source_path = NULL;
    pkg->source_dir = source_dir;
    pkg->source_text = pf->source_text;     pf->source_text = NULL;
    pkg->ast = pf->ast;                     pf->ast = NULL;
    pkg->package_name = pf->package_name;   pf->package_name = NULL;
    pkg->analysed = false;

    parsed_file_free(pf);

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
