#pragma once

#include "generator_lists.h"
#include "odin_grammar_ast.h"
#include "package_resolver.h"
#include "polymorphism.h"
#include "sem_error.h"
#include "type_descriptors.h"

#include <easy_pc/easy_pc.h>
#include <stddef.h>

// Forward declarations
typedef struct epc_ast_hook_registry_t epc_ast_hook_registry_t;

typedef struct SemContext
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

    // Polymorphic-procedure instantiation guard. Set to true while the
    // semantic analyser is processing a *specialization* (a cloned AST
    // whose `$T`/`$N` poly idents have been substituted with concrete
    // types/values). The flag suppresses the polymorphism early-return
    // in sem_analyse_procedure_literal so the specialization's body is
    // analyzed normally.
    bool currently_instantiating;

    // Stage 12: Expected return type for poly calls where `$T` appears in
    // the return position only (no param binding from args). The
    // surrounding variable declaration (e.g. `r: int = poly_call()`)
    // threads its declared type down via this field so `poly_resolve_call`
    // can use it as a fallback binding for the return-position poly var.
    // NULL most of the time; set transiently around `sem_evaluate_expr`
    // call sites that have an expected type. Always reset to NULL after.
    TypeDescriptor const * poly_expected_return_type;

    // Poly env stack (env-stack approach — no AST clone).
    // Dynamic array: each entry is a PolyEnv with type/int bindings for poly vars.
    PolyEnv * poly_env_stack;
    int poly_env_stack_depth;
    int poly_env_stack_capacity;

    // Pending specializations generated during semantic pass.
    // Codegen drains these after the main top-level pass.
    PolySpecialization ** pending_specializations;
    int pending_spec_count;
    int pending_spec_capacity;
} SemContext;

#include "sem_context.h"

// Compile-time constant evaluation (used across multiple modules)
long long sem_evaluate_constant_int(SemContext * ctx, odin_grammar_node_t * node, int * ok);
int sem_evaluate_constant_bool(SemContext * ctx, odin_grammar_node_t * node);

bool sem_analyse(SemContext * ctx);
