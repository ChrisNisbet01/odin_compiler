#pragma once

#include "generator_lists.h"
#include "odin_grammar_ast.h"
#include "package_resolver.h"
#include "sem_error.h"
#include "type_descriptors.h"

#include <easy_pc/easy_pc.h>
#include <stddef.h>

// Forward declarations
typedef struct epc_ast_hook_registry_t epc_ast_hook_registry_t;

typedef struct
{
    odin_grammar_node_t * ast;
    TypeDescriptors * type_registry;
    GeneratorContext * gen_ctx;
    SemErrorList errors;

    // Package/import support
    char const * source_file_path; // the main source file path
    char const * source_dir;       // directory of the main source file
    char const * odin_root;        // resolved ODIN_ROOT path
    char * package_name;           // extracted from package clause
    ImportedPackage ** imports;    // array of imported packages
    int import_count;
    int import_capacity;

    // Parser/hooks for parsing imported files
    epc_parser_t * parser;
    epc_ast_hook_registry_t * hook_registry;
} SemContext;

void sem_context_init(
    SemContext * ctx,
    odin_grammar_node_t * ast,
    TypeDescriptors * type_registry,
    GeneratorContext * gen_ctx,
    char const * source_file_path,
    char const * source_dir,
    char const * odin_root,
    epc_parser_t * parser,
    epc_ast_hook_registry_t * hooks
);

void sem_context_destroy(SemContext * ctx);

bool sem_analyse(SemContext * ctx);
