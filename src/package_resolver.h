#pragma once

#include "odin_grammar_ast.h"

#include <easy_pc/easy_pc.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct epc_ast_hook_registry_t epc_ast_hook_registry_t;
typedef struct scope scope_t;

// Result of parsing a single source file
typedef struct
{
    char * source_path;          // full path (owned)
    char * source_text;          // file text (owned)
    odin_grammar_node_t * ast;   // full program AST (owned)
    char * package_name;         // from package clause (owned)
} ParsedFile;

ParsedFile * parsed_file_create(char const * source_path);
void parsed_file_free(ParsedFile * pf);

// Parse a single source file, setting file_path on every AST node.
// Returns NULL on failure (prints errors to stderr).
ParsedFile * parse_source_file(char const * path, epc_parser_t * parser,
                                epc_ast_hook_registry_t * hooks);

// Merge multiple parsed file ASTs into a single Program AST.
// Children are moved (stolen) from each ParsedFile's AST.
// On success, the returned AST owns all children; the ParsedFiles can be freed.
// On failure, returns NULL and sets *out_error.
odin_grammar_node_t * merge_program_asts(ParsedFile ** files, int file_count,
                                          char ** out_error);

typedef struct
{
    char * source_path;        // full path to the imported file
    char * source_dir;         // directory containing the file
    char * source_text;        // the file's text (owned by this struct)
    odin_grammar_node_t * ast; // the parsed AST (owned by this struct)
    char * package_name;       // from the package clause
    scope_t * package_scope;   // scope holding the package's exported symbols
    bool analysed;             // has semantic analysis been run on this?
    bool codegen_done;         // has IR codegen been done for this package?
    bool is_using;             // was this imported with 'import using'?
    bool is_runtime;           // true for auto-imported core:runtime prelude
} ImportedPackage;

// Resolve an import path to a file or directory path.
// Returns a heap-allocated string to the resolved path, or NULL if not found.
// The returned path ends in ".odin" for single-file packages, or is a directory
// path for multi-file packages (caller checks with has_odin_extension() or similar).
char * resolve_import_path(char const * import_name, char const * source_dir, char const * odin_root);

// Parse a single file and return an ImportedPackage.
// Returns NULL on any error.
ImportedPackage * parse_imported_file(char const * file_path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks);

// Parse a directory of .odin files as a single package, merging their ASTs.
// Returns NULL on any error.
ImportedPackage * parse_imported_directory(char const * dir_path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks);

// Free all resources associated with an ImportedPackage.
void imported_package_free(ImportedPackage * pkg);

// Resolve ODIN_ROOT from environment or default relative to the executable path.
// exe_path should be argv[0].
// Returns a heap-allocated string.
char * resolve_odin_root(char const * exe_path);
