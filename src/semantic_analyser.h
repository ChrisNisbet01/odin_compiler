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

    // Import cycle detection — stack of paths currently being processed
    char ** import_stack;
    int import_stack_count;
    int import_stack_capacity;
} SemContext;

#include "sem_context.h"

// Compile-time constant evaluation (used across multiple modules)
long long sem_evaluate_constant_int(SemContext * ctx, odin_grammar_node_t * node, int * ok);
int sem_evaluate_constant_bool(SemContext * ctx, odin_grammar_node_t * node);

bool sem_analyse(SemContext * ctx);
