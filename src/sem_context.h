#pragma once

#include "semantic_analyser.h"

#include <stdbool.h>

// SemContext lifecycle
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

// Calling convention parsing
calling_convention_t parse_calling_convention(char const * text);

// Vector swizzle validation
bool is_valid_swizzle(char const * field, int lane_count);

// Strip quotes from StringLiteral text (mutates: returns allocated string)
char * strip_quotes(char const * text);

// Find imported package by name
ImportedPackage * find_imported_package_by_name(SemContext * ctx, char const * name);

// Walk a comma-chain Expression tree to collect individual argument nodes
void sem_collect_comma_chain_args(odin_grammar_node_t * node, odin_grammar_node_t ** out_args, int max_args, int * out_count);

// Polymorphic-procedure instantiation: re-analyses a procedure definition
// with the poly env stack active (called from polymorphism.c).
void sem_analyse_procedure_literal(SemContext * ctx, odin_grammar_node_t * node, char const * proc_name);
