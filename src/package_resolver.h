#pragma once

#include "odin_grammar_ast.h"

#include <easy_pc/easy_pc.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct epc_ast_hook_registry_t epc_ast_hook_registry_t;
typedef struct scope scope_t;

typedef struct
{
    char * source_path;        // full path to the imported file
    char * source_dir;         // directory containing the file
    char * source_text;        // the file's text (owned by this struct)
    odin_grammar_node_t * ast; // the parsed AST (owned by this struct)
    char * package_name;       // from the package clause
    scope_t * package_scope;   // scope holding the package's exported symbols
    bool analysed;             // has semantic analysis been run on this?
} ImportedPackage;

// Resolve an import path to a file path.
// Search order:
//   1. Relative to source_dir/<import_name>/<import_name>.odin
//   2. Relative to source_dir/<import_name>.odin
//   3. <odin_root>/src/<import_name>/<import_name>.odin
//   4. <odin_root>/src/<import_name>.odin
// Returns a heap-allocated string to the resolved path, or NULL if not found.
char * resolve_import_path(char const * import_name, char const * source_dir, char const * odin_root);

// Parse a file and return an ImportedPackage.
// The parser and hook_registry must outlive the returned package's AST usage.
// Returns NULL on any error (parsing failure, AST build failure, etc.).
ImportedPackage * parse_imported_file(char const * file_path, epc_parser_t * parser, epc_ast_hook_registry_t * hooks);

// Free all resources associated with an ImportedPackage.
void imported_package_free(ImportedPackage * pkg);

// Resolve ODIN_ROOT from environment or default relative to the executable path.
// exe_path should be argv[0].
// Returns a heap-allocated string.
char * resolve_odin_root(char const * exe_path);
