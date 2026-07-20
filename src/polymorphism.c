#include "polymorphism.h"

#include "ast_utils.h"
#include "scope.h"
#include "sem_context.h"
#include "semantic_analyser.h"
#include "symbols.h"
#include "typed_value.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// Stage 1: detection
// =========================================================================

static bool
poly_walk_has_ident(odin_grammar_node_t const * node)
{
    if (node == NULL)
        return false;
    if (node->type == AST_NODE_POLY_IDENT)
        return true;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (poly_walk_has_ident(node->list.children[i]))
            return true;
    }
    return false;
}

bool
poly_signature_is_polymorphic(odin_grammar_node_t const * sig_node)
{
    return poly_walk_has_ident(sig_node);
}

// =========================================================================
// Side table: symbol_t* -> ConstantDecl AST node (origin tracking)
// =========================================================================

typedef struct
{
    symbol_t * sym;
    odin_grammar_node_t * const_decl;
} PolyOriginEntry;

static PolyOriginEntry * poly_origins = NULL;
static int poly_origin_count = 0;
static int poly_origin_capacity = 0;

void
poly_register_origin(symbol_t * sym, odin_grammar_node_t * const_decl)
{
    if (sym == NULL)
        return;
    // Check if already registered
    for (int i = 0; i < poly_origin_count; i++)
    {
        if (poly_origins[i].sym == sym)
        {
            poly_origins[i].const_decl = const_decl;
            return;
        }
    }
    // Add new entry
    if (poly_origin_count >= poly_origin_capacity)
    {
        int new_cap = poly_origin_capacity == 0 ? 8 : poly_origin_capacity * 2;
        PolyOriginEntry * tmp = realloc(poly_origins, (size_t)new_cap * sizeof(PolyOriginEntry));
        if (tmp == NULL) return;
        poly_origins = tmp;
        poly_origin_capacity = new_cap;
    }
    poly_origins[poly_origin_count].sym = sym;
    poly_origins[poly_origin_count].const_decl = const_decl;
    poly_origin_count++;
}

odin_grammar_node_t *
poly_get_origin(symbol_t * sym)
{
    if (sym == NULL)
        return NULL;
    for (int i = 0; i < poly_origin_count; i++)
    {
        if (poly_origins[i].sym == sym)
            return poly_origins[i].const_decl;
    }
    return NULL;
}

// =========================================================================
// Env stack management
// =========================================================================

void
poly_env_push(SemContext * ctx, PolyEnv * env)
{
    assert(ctx->poly_env_stack_depth < MAX_POLY_STACK_DEPTH);
    ctx->poly_env_stack[ctx->poly_env_stack_depth] = *env;
    ctx->poly_env_stack_depth++;
}

void
poly_env_pop(SemContext * ctx)
{
    assert(ctx->poly_env_stack_depth > 0);
    ctx->poly_env_stack_depth--;
    // Free any strdup'd entry names in the popped env
    PolyEnv * env = &ctx->poly_env_stack[ctx->poly_env_stack_depth];
    for (int i = 0; i < env->count; i++)
    {
        if (env->entries[i].name)
        {
            free((void *)env->entries[i].name);
            env->entries[i].name = NULL;
        }
    }
}

TypeDescriptor const *
poly_env_lookup_type(SemContext * ctx, char const * name)
{
    if (name == NULL)
        return NULL;
    for (int i = ctx->poly_env_stack_depth - 1; i >= 0; i--)
    {
        PolyEnv * env = &ctx->poly_env_stack[i];
        for (int j = 0; j < env->count; j++)
        {
            if (env->entries[j].kind == POLY_SLOT_TYPE
                && strcmp(env->entries[j].name, name) == 0)
            {
                return env->entries[j].bound_type;
            }
        }
    }
    return NULL;
}

bool
poly_env_lookup_int(SemContext * ctx, char const * name, long long * out_val)
{
    if (name == NULL || out_val == NULL)
        return false;
    for (int i = ctx->poly_env_stack_depth - 1; i >= 0; i--)
    {
        PolyEnv * env = &ctx->poly_env_stack[i];
        for (int j = 0; j < env->count; j++)
        {
            if (env->entries[j].kind == POLY_SLOT_INT
                && strcmp(env->entries[j].name, name) == 0)
            {
                *out_val = env->entries[j].bound_int_value;
                return true;
            }
        }
    }
    return false;
}

// =========================================================================
// Helper: walk a subtree to find the first Identifier node
// =========================================================================

static odin_grammar_node_t *
poly_find_ident_in_subtree(odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;
    if (node->type == AST_NODE_IDENTIFIER)
        return node;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * found = poly_find_ident_in_subtree(node->list.children[i]);
        if (found)
            return found;
    }
    return NULL;
}

// =========================================================================
// Build PolyEnv from call-site argument types
// =========================================================================

// Forward declaration used here (defined in semantic_analyser.c but we
// need it in sem_evaluate_expr.c too — actually, this module already
// includes semantic_analyser.h, so sem_evaluate_expr should be available).
// But we don't want to call sem_evaluate_expr from here directly.
// Instead, the caller (sem_evaluate_expr.c) evaluates args BEFORE calling
// poly_build_env_from_args, and passes the arg_list_node with resolved types.

bool
poly_build_env_from_args(
    SemContext * ctx,
    symbol_t * poly_symbol,
    odin_grammar_node_t * proc_def_node,
    odin_grammar_node_t * arg_list_node,
    PolyEnv * out_env
)
{
    memset(out_env, 0, sizeof(PolyEnv));
    if (proc_def_node == NULL)
        return false;

    // Find the procedure signature and parameter list inside it
    odin_grammar_node_t * param_list_node = NULL;
    for (size_t i = 0; i < proc_def_node->list.count; i++)
    {
        odin_grammar_node_t * child = proc_def_node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_PROCEDURE_SIGNATURE)
        {
            for (size_t j = 0; j < child->list.count; j++)
            {
                odin_grammar_node_t * sig_child = child->list.children[j];
                if (sig_child && sig_child->type == AST_NODE_PARAMETER_LIST)
                {
                    param_list_node = sig_child;
                    break;
                }
            }
            break;
        }
    }
    if (param_list_node == NULL)
        return false;

    // Collect arg types from the call site
    TypeDescriptor const * arg_types[MAX_POLY_ENV_ENTRIES];
    int arg_count = 0;
    if (arg_list_node && arg_list_node->type == AST_NODE_ARGUMENT_LIST)
    {
        for (size_t ai = 0; ai < arg_list_node->list.count && arg_count < MAX_POLY_ENV_ENTRIES; ai++)
        {
            odin_grammar_node_t * arg_node = arg_list_node->list.children[ai];
            if (arg_node == NULL)
                continue;
            arg_types[arg_count] = arg_node->resolved_type;
            arg_count++;
        }
    }

    // Get the PARAMETERS node
    if (param_list_node->list.count == 0)
        return arg_count == 0;
    odin_grammar_node_t * params = param_list_node->list.children[0];
    if (params == NULL || params->type != AST_NODE_PARAMETERS)
        return false;

    int param_idx = 0;
    for (size_t k = 0; k < params->list.count; k++)
    {
        odin_grammar_node_t * param = params->list.children[k];
        if (param == NULL || param->type != AST_NODE_PARAMETER)
            continue;

        // Check for ellipsis (variadic marker)
        bool is_variadic = false;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            if (param->list.children[ci]
                && param->list.children[ci]->type == AST_NODE_ELLIPSIS)
            {
                is_variadic = true;
                break;
            }
        }
        if (is_variadic)
        {
            // For variadic ..T params, bind the element type for each extra arg
            // For ..any, there's no poly binding needed
            // For ..$T, bind T to the type of each extra arg (but we only
            // handle the first extra arg for simplicity in Stage 3)
            odin_grammar_node_t * poly_type_node = NULL;
            for (size_t ci = 0; ci < param->list.count; ci++)
            {
                odin_grammar_node_t * child = param->list.children[ci];
                if (child && child->type == AST_NODE_POLY_IDENT)
                {
                    poly_type_node = child;
                    break;
                }
            }
            if (poly_type_node)
            {
                // Strip $ prefix from poly ident text
                char const * poly_name = poly_type_node->text;
                if (poly_name && poly_name[0] == '$')
                    poly_name = poly_name + 1;
                if (poly_name && param_idx < arg_count)
                {
                    bool already = false;
                    for (int ei = 0; ei < out_env->count; ei++)
                    {
                        if (strcmp(out_env->entries[ei].name, poly_name) == 0)
                        {
                            already = true;
                            break;
                        }
                    }
                    if (!already && out_env->count < MAX_POLY_ENV_ENTRIES)
                    {
                        out_env->entries[out_env->count].name = strdup(poly_name);
                        out_env->entries[out_env->count].kind = POLY_SLOT_TYPE;
                        out_env->entries[out_env->count].bound_type = arg_types[param_idx];
                        out_env->count++;
                    }
                }
            }
            // Variadic consumes remaining args conceptually, but we don't
            // increment param_idx for variadic marker
            continue;
        }

        // Find the type node for this param and check if its type is a poly ident reference
        // (For the $T: typeid case with the new grammar, PolyIdent is in the name position
        //  and should be bound to the arg type for this param slot)
        odin_grammar_node_t * poly_type_node = NULL;
        odin_grammar_node_t * param_type = NULL;
        bool has_poly_decl = false;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_POLY_IDENT)
            {
                // This parameter IS a poly type declaration ($T: typeid)
                // Bind $T to the arg type for the NEXT non-poly param
                char const * pn = child->text;
                if (pn && pn[0] == '$')
                    pn = pn + 1;
                if (pn && out_env->count < MAX_POLY_ENV_ENTRIES)
                {
                    // Check if already bound
                    bool already = false;
                    for (int ei = 0; ei < out_env->count; ei++)
                    {
                        if (strcmp(out_env->entries[ei].name, pn) == 0)
                        {
                            already = true;
                            break;
                        }
                    }
                    if (!already)
                    {
                        // Placeholder — will be bound when we see the type usage
                        out_env->entries[out_env->count].name = strdup(pn);
                        out_env->entries[out_env->count].kind = POLY_SLOT_TYPE;
                        out_env->entries[out_env->count].bound_type = NULL;
                        out_env->count++;
                    }
                }
                poly_type_node = child;
                has_poly_decl = true;
            }
        }

        if (has_poly_decl)
        {
            // $T: typeid — the poly type declaration doesn't consume an arg slot
            // (it's a compile-time construct). Skip param_idx increment.
            continue;
        }

        // Find Identifier referencing a poly type in the parameter's type position
        // For non-poly params (x: T), the type T is an Identifier that should match
        // a poly param name. If so, bind the poly name to this arg's type.
        // NOTE: TypePrefix rule has no action, so its matched nodes are flattened
        // into the Parameter children. For x: T, the children are:
        // [Identifier("x"), Identifier("T")] — "T" is the second identifier.
        // The param name is the first Identifier child; subsequent Identifiers are types.
        int poly_ident_count = 0;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL)
                continue;
            // Count Identifier children that could be poly type references
            // (the first Identifier is the param name, skip it)
            if (child->type == AST_NODE_IDENTIFIER)
            {
                poly_ident_count++;
                if (poly_ident_count <= 1)
                    continue; // skip param name
            }
            // Check for type nodes or additional identifiers
            if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
            {
                odin_grammar_node_t * found = child;
                if (is_type_node(child))
                    found = poly_find_ident_in_subtree(child);
                if (found && found->type == AST_NODE_IDENTIFIER)
                {
                    if (param_idx < arg_count)
                    {
                        char const * candidate = found->text;
                        if (candidate)
                        {
                            for (int ei = 0; ei < out_env->count; ei++)
                            {
                                if (strcmp(out_env->entries[ei].name, candidate) == 0
                                    && out_env->entries[ei].bound_type == NULL)
                                {
                                    out_env->entries[ei].bound_type = arg_types[param_idx];
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            }
        }

        param_idx++;
    }

    return out_env->count > 0;
}

// =========================================================================
// Mangled name generation
// =========================================================================

// Forward declaration from type_descriptors.c
void type_write_canonical_name(TypeDescriptor const * td, char * buf, size_t cap);

char *
poly_make_mangled_name(symbol_t * poly_symbol, PolyEnv * env)
{
    // Estimate buffer: base name + "_poly_" + canonical names + separators
    char buf[512];
    int pos = 0;

    // Origin name
    char const * base = poly_symbol ? poly_symbol->name : "unknown";
    while (*base && pos < (int)sizeof(buf) - 1)
        buf[pos++] = *base++;

    // Append type bindings
    char const * sep = "__poly_";
    for (int i = 0; i < env->count; i++)
    {
        // Append separator
        for (char const * s = sep; *s && pos < (int)sizeof(buf) - 1; s++)
            buf[pos++] = *s;
        sep = "_";

        if (env->entries[i].kind == POLY_SLOT_TYPE && env->entries[i].bound_type)
        {
            char type_buf[128];
            type_write_canonical_name(env->entries[i].bound_type, type_buf, sizeof(type_buf));
            for (char const * s = type_buf; *s && pos < (int)sizeof(buf) - 1; s++)
                buf[pos++] = *s;
        }
        else if (env->entries[i].kind == POLY_SLOT_INT)
        {
            char int_buf[64];
            snprintf(int_buf, sizeof(int_buf), "%lld", env->entries[i].bound_int_value);
            for (char const * s = int_buf; *s && pos < (int)sizeof(buf) - 1; s++)
                buf[pos++] = *s;
        }
    }
    buf[pos] = '\0';
    return strdup(buf);
}

// =========================================================================
// poly_resolve_call — the core instantiation logic
// =========================================================================

PolySpecialization *
poly_resolve_call(
    SemContext * ctx,
    symbol_t * poly_symbol,
    odin_grammar_node_t * call_op,
    odin_grammar_node_t * arg_list_node
)
{
    if (poly_symbol == NULL)
        return NULL;

    // Get the origin ConstantDecl
    odin_grammar_node_t * const_decl = poly_get_origin(poly_symbol);
    if (const_decl == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, call_op,
                           "polymorphic procedure has no origin AST");
        return NULL;
    }

    // Find the ProcedureDefinition inside the ConstantDecl
    odin_grammar_node_t * proc_def = NULL;
    for (size_t i = 0; i < const_decl->list.count; i++)
    {
        odin_grammar_node_t * child = const_decl->list.children[i];
        if (child && child->type == AST_NODE_PROCEDURE_DEFINITION)
        {
            proc_def = child;
            break;
        }
    }
    if (proc_def == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, call_op,
                           "polymorphic origin has no procedure definition");
        return NULL;
    }

    // Build PolyEnv from arg types
    PolyEnv env;
    if (!poly_build_env_from_args(ctx, poly_symbol, proc_def, arg_list_node, &env))
    {
        sem_error_list_add(&ctx->errors, NULL, call_op,
                           "failed to build polymorphic environment from arguments");
        return NULL;
    }

    // Generate mangled name
    char * mangled_name = poly_make_mangled_name(poly_symbol, &env);

    // Push env onto stack
    poly_env_push(ctx, &env);
    ctx->currently_instantiating = true;

    // Reset resolved_type/resolved_symbol on proc definition and its body
    // so the re-analysis is clean
    // (We only need to reset the proc_def and its children — the body children
    //  were never analyzed in Stage 2 thanks to the early-return)

    // Run sem_analyse_procedure_literal on the original proc definition
    sem_analyse_procedure_literal(ctx, proc_def, mangled_name);

    // Pop env
    ctx->currently_instantiating = false;
    poly_env_pop(ctx);

    // Get the concrete proc type from the resolved procedure definition
    TypeDescriptor const * concrete_proc_type = proc_def->resolved_type;
    if (concrete_proc_type == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, call_op,
                           "polymorphic instantiation failed — procedure type is NULL");
        free(mangled_name);
        return NULL;
    }

    // Check if a specialization with this name already exists in the global scope.
    // We deliberately look ONLY in global scope (not walking the scope chain) because
    // sem_analyse_procedure_literal's forward-reference code at line ~727 also registers
    // the mangled name in the parent (current) scope, and that copy is freed when the
    // scope is popped — using it would create a use-after-free in op->resolved_symbol.
    symbol_t * existing = scope_symbols_lookup_entry_by_name(
        &ctx->gen_ctx->global_scope->symbols, mangled_name);
    if (existing)
    {
        // Already specialized (Stage 5 cache hit; for Stage 3 without cache,
        // this shouldn't normally happen within the same file unless
        // instantiation was already triggered)
        PolySpecialization * spec = calloc(1, sizeof(PolySpecialization));
        if (spec)
        {
            spec->symbol = existing;
            spec->origin_const_decl = const_decl;
        }
        free(mangled_name);
        return spec;
    }

    // Register a new specialization symbol with the mangled name
    // Use global scope so the symbol survives scope pops during semantic analysis
    // and remains accessible via resolved_symbol pointers during IR generation.
    TypedValue tv = create_typed_value(NULL, concrete_proc_type, false);
    scope_add_symbol(ctx->gen_ctx->global_scope, mangled_name, tv);

    symbol_t * spec_sym = scope_symbols_lookup_entry_by_name(
        &ctx->gen_ctx->global_scope->symbols, mangled_name);
    if (spec_sym == NULL)
    {
        sem_error_list_add(&ctx->errors, NULL, call_op,
                           "failed to register specialization symbol");
        free(mangled_name);
        return NULL;
    }

    // Add to pending specializations for codegen
    PolySpecialization * spec = calloc(1, sizeof(PolySpecialization));
    if (spec)
    {
        spec->symbol = spec_sym;
        spec->origin_const_decl = const_decl;

        // Enqueue for codegen
        if (ctx->pending_spec_count >= ctx->pending_spec_capacity)
        {
            int new_cap = ctx->pending_spec_capacity == 0 ? 8 : ctx->pending_spec_capacity * 2;
            PolySpecialization ** tmp = realloc(
                ctx->pending_specializations,
                (size_t)new_cap * sizeof(PolySpecialization *));
            if (tmp)
            {
                ctx->pending_specializations = tmp;
                ctx->pending_spec_capacity = new_cap;
            }
        }
        if (ctx->pending_spec_count < ctx->pending_spec_capacity)
        {
            ctx->pending_specializations[ctx->pending_spec_count] = spec;
            ctx->pending_spec_count++;
        }
    }

    free(mangled_name);
    return spec;
}
