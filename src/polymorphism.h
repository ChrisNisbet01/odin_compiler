#pragma once

// Polymorphic-procedure (generics) support for the Odin compiler.
//
// A polymorphic procedure is one whose signature contains one or more
// compile-time parameters prefixed with `$` (e.g. `x: $T` declares a
// type-polymorphic parameter named T). Polymorphic procs are NOT analyzed or
// codegen'd directly. Instead, each call site instantiates a specialization
// by binding the $-parameters from the concrete argument types/values,
// pushing a PolyEnv onto the SemContext poly env stack, and re-analysing the
// original (un-cloned) proc AST with the bindings active.
//
// This module owns:
//   * Detection of polymorphic signatures (`poly_signature_is_polymorphic`).
//   * Poly env stack management (push/pop/lookup).
//   * Origin AST tracking (side table keyed by symbol_t*).
//   * Call-site instantiation (`poly_resolve_call`).
//   * (Stage 5) Specialization cache.
//   * (Stage 6) Shared matching for polymorphic-overload-bundle candidates.

#include "odin_grammar_ast.h"
#include "type_descriptors.h"

// Forward declarations (SemContext is defined in semantic_analyser.h)
struct SemContext;
typedef struct symbol symbol_t;

// --- Poly env ---

typedef enum
{
    POLY_SLOT_TYPE,
    POLY_SLOT_INT,
} PolySlotKind;

#define MAX_POLY_ENV_ENTRIES 16
#define MAX_POLY_STACK_DEPTH 8

typedef struct
{
    char const * name;               // "T", "N", etc.
    PolySlotKind kind;
    TypeDescriptor const * bound_type;     // for POLY_SLOT_TYPE
    long long bound_int_value;            // for POLY_SLOT_INT
} PolyEnvEntry;

typedef struct
{
    PolyEnvEntry entries[MAX_POLY_ENV_ENTRIES];
    int count;
} PolyEnv;

// --- Env stack management ---
// SemContext owns the stack array (poly_env_stack[], poly_env_stack_depth).

void poly_env_push(struct SemContext * ctx, PolyEnv * env);
void poly_env_pop(struct SemContext * ctx);

// Look up a type binding in the poly env stack (searches from top down).
// Returns NULL if no binding found.
TypeDescriptor const * poly_env_lookup_type(struct SemContext * ctx, char const * name);

// Look up an integer binding in the poly env stack.
bool poly_env_lookup_int(struct SemContext * ctx, char const * name, long long * out_val);

// --- Origin tracking (side table: symbol_t* -> ConstantDecl AST node) ---

void poly_register_origin(symbol_t * sym, odin_grammar_node_t * const_decl);
odin_grammar_node_t * poly_get_origin(symbol_t * sym);

// --- Instantiation result ---

typedef struct PolySpecialization
{
    symbol_t * symbol;          // the specialization symbol (with concrete type_info)
    odin_grammar_node_t * origin_const_decl;  // the original poly ConstantDecl
    TypeDescriptor const ** param_types;      // specialization-specific param types
    int param_count;                          // number of param types stored
    char const ** poly_int_names;             // strdup'd names of $N params
    long long * poly_int_values;             // corresponding integer values
    int poly_int_count;                      // number of poly int params
} PolySpecialization;

// --- Call resolution ---

// For a poly proc call, build PolyEnv from arg types, push onto env stack,
// re-analyse the original proc body, register a mangled specialization
// symbol, and return the result.
PolySpecialization * poly_resolve_call(
    struct SemContext * ctx,
    symbol_t * poly_symbol,
    odin_grammar_node_t * call_op,
    odin_grammar_node_t * arg_list_node
);

// Build a PolyEnv by matching concrete argument types against the poly
// procedure's parameter type nodes. Returns true if at least one binding
// was created.
bool poly_build_env_from_args(
    struct SemContext * ctx,
    symbol_t * poly_symbol,
    odin_grammar_node_t * proc_def_node,
    odin_grammar_node_t * arg_list_node,
    PolyEnv * out_env
);

// --- Mangled name generation ---

// Generate a mangled name for a specialization symbol.
// Format: <origin_name>__poly_<argtypecanon1>_<argtypecanon2>...
char * poly_make_mangled_name(symbol_t * poly_symbol, PolyEnv * env);

// True if the given procedure-signature node references ANY `$T`/`$N`
// polymorphic identifier (in parameters or returns). Used at the top-level
// registration site to decide whether to mark the proc symbol polymorphic.
bool poly_signature_is_polymorphic(odin_grammar_node_t const * sig_node);
