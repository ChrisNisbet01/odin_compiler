#include "polymorphism.h"

#include "ast_metadata.h"
#include "ast_utils.h"
#include "scope.h"
#include "sem_context.h"
#include "semantic_analyser.h"
#include "sem_type_resolver.h"
#include "symbols.h"
#include "typed_value.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =========================================================================
// Stage 9: where-clause evaluation
// =========================================================================

// Resolve a type expression in the where-clause context.
// Checks poly env first (for poly idents like $T / T), then falls back
// to the normal type resolution path (for concrete types like int).
static TypeDescriptor const *
poly_resolve_type_for_where(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

    // Unwrap AST_NODE_TYPE_NAME to get to the underlying type node.
    while (node != NULL && node->type == AST_NODE_TYPE_NAME && node->list.count >= 1)
        node = node->list.children[0];

    if (node == NULL)
        return NULL;

    if (node->type == AST_NODE_IDENTIFIER)
    {
        char const * name = node->text;
        if (name != NULL)
        {
            char const * lookup = (name[0] == '$') ? name + 1 : name;
            TypeDescriptor const * td = poly_env_lookup_type(ctx, lookup);
            if (td != NULL)
                return td;
        }
    }

    if (node->type == AST_NODE_POLY_IDENT)
    {
        char const * name = node->text;
        if (name != NULL)
        {
            char const * lookup = (name[0] == '$') ? name + 1 : name;
            TypeDescriptor const * td = poly_env_lookup_type(ctx, lookup);
            if (td != NULL)
                return td;
        }
    }

    // Fall back to normal type resolution
    TypeDescriptor const * result = sem_resolve_type_expr(ctx, node);
    return result;
}

// Evaluate typeid_of(T) in the where-clause context: resolve T via poly env
// or type registry, then return its type_id.
static long long
poly_eval_typeid_of(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1 || node->list.children[0] == NULL)
        return -1;
    odin_grammar_node_t * type_node = node->list.children[0];
    TypeDescriptor const * td = poly_resolve_type_for_where(ctx, type_node);
    if (td == NULL)
        return -1;
    return td->type_id;
}

// Evaluate size_of(T) in the where-clause context.
static long long
poly_eval_size_of(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1 || node->list.children[0] == NULL)
        return -1;
    odin_grammar_node_t * type_node = node->list.children[0];
    TypeDescriptor const * td = poly_resolve_type_for_where(ctx, type_node);
    if (td == NULL || td->llvm_type == NULL)
        return -1;
    LLVMTargetDataRef dl = type_descriptors_get_data_layout(ctx->type_registry);
    if (dl == NULL)
        return -1;
    return (long long)LLVMABISizeOfType(dl, td->llvm_type);
}

// Evaluate a where-clause sub-expression, returning a concrete value.
// Returns -1 if the expression cannot be evaluated at compile time.
static long long
poly_eval_where_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return -1;

    // Unwrap single-child expression wrappers
    while (node != NULL && node->list.count == 1
           && node->list.children[0] != NULL
           && node->type != AST_NODE_TYPEID_OF_EXPR
           && node->type != AST_NODE_SIZE_OF_EXPR
           && node->type != AST_NODE_INTEGER_VALUE
           && node->type != AST_NODE_BOOL_TRUE
           && node->type != AST_NODE_BOOL_FALSE
           && node->type != AST_NODE_IDENTIFIER)
    {
        node = node->list.children[0];
    }

    switch (node->type)
    {
    case AST_NODE_BOOL_TRUE:
        return 1;
    case AST_NODE_BOOL_FALSE:
        return 0;

    case AST_NODE_INTEGER_VALUE:
    {
        int ok = 0;
        long long val = sem_evaluate_constant_int(ctx, node, &ok);
        return ok ? val : -1;
    }

    case AST_NODE_TYPEID_OF_EXPR:
        return poly_eval_typeid_of(ctx, node);

    case AST_NODE_SIZE_OF_EXPR:
        return poly_eval_size_of(ctx, node);

    case AST_NODE_IDENTIFIER:
    {
        // Check poly int env (for $N params)
        if (node->text != NULL)
        {
            long long val = 0;
            if (poly_env_lookup_int(ctx, node->text, &val))
                return val;
        }
        return -1;
    }

    // Binary operators
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    {
        if (node->list.count < 3)
            return -1;

        // Find the operator node (middle child)
        odin_grammar_node_t * op_node = node->list.children[1];
        if (op_node == NULL)
            return -1;
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL)
            return -1;

        long long lhs = poly_eval_where_expr(ctx, node->list.children[0]);
        long long rhs = poly_eval_where_expr(ctx, node->list.children[node->list.count - 1]);
        if (lhs == -1 && md->kind != OP_UNARY_NOT)
            return -1;
        if (rhs == -1 && md->kind != OP_UNARY_NOT)
            return -1;

        switch (md->kind)
        {
        case OP_EQ: return (lhs == rhs) ? 1 : 0;
        case OP_NE: return (lhs != rhs) ? 1 : 0;
        case OP_LT: return (lhs < rhs) ? 1 : 0;
        case OP_GT: return (lhs > rhs) ? 1 : 0;
        case OP_LE: return (lhs <= rhs) ? 1 : 0;
        case OP_GE: return (lhs >= rhs) ? 1 : 0;
        case OP_ADD: return lhs + rhs;
        case OP_SUB: return lhs - rhs;
        case OP_MUL: return lhs * rhs;
        case OP_DIV: return (rhs == 0) ? -1 : lhs / rhs;
        case OP_MOD: return (rhs == 0) ? -1 : lhs % rhs;
        case OP_BIT_AND: return lhs & rhs;
        case OP_BIT_OR:  return lhs | rhs;
        case OP_BIT_XOR: return lhs ^ rhs;
        case OP_SHL: return lhs << rhs;
        case OP_SHR: return (rhs < 0 || rhs >= 64) ? -1 : (lhs >> rhs);
        case OP_LOG_AND: return (lhs != 0 && rhs != 0) ? 1 : 0;
        case OP_LOG_OR:  return (lhs != 0 || rhs != 0) ? 1 : 0;
        default: return -1;
        }
    }

    // Unary operators
    case AST_NODE_UNARY_EXPRESSION:
    {
        if (node->list.count < 2)
            return -1;
        odin_grammar_node_t * op_node = node_find_op(node);
        if (op_node == NULL)
            return -1;
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL)
            return -1;

        odin_grammar_node_t * operand = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            if (node->list.children[i] != NULL && node->list.children[i] != op_node)
            { operand = node->list.children[i]; break; }
        }
        if (operand == NULL)
            return -1;

        long long inner = poly_eval_where_expr(ctx, operand);
        if (inner == -1)
            return -1;

        switch (md->kind)
        {
        case OP_UNARY_NOT: return inner ? 0 : 1;
        case OP_UNARY_NEG: return -inner;
        case OP_UNARY_POS: return inner;
        case OP_UNARY_XOR: return ~inner;
        default: return -1;
        }
    }

    default:
        return -1;
    }
}

// Find the AST_NODE_WHERE_CLAUSE inside a procedure definition's signature.
static odin_grammar_node_t *
poly_find_where_clause(odin_grammar_node_t * proc_def_node)
{
    if (proc_def_node == NULL)
        return NULL;

    odin_grammar_node_t * sig = NULL;
    for (size_t i = 0; i < proc_def_node->list.count; i++)
    {
        odin_grammar_node_t * child = proc_def_node->list.children[i];
        if (child != NULL && child->type == AST_NODE_PROCEDURE_SIGNATURE)
        { sig = child; break; }
    }
    if (sig == NULL)
        return NULL;

    for (size_t i = 0; i < sig->list.count; i++)
    {
        odin_grammar_node_t * child = sig->list.children[i];
        if (child != NULL && child->type == AST_NODE_WHERE_CLAUSE)
            return child;
    }
    return NULL;
}

// Evaluate a where clause in the context of the poly env stack.
// Returns true if the constraint is satisfied, false if violated.
// Returns true (vacuously satisfied) if no where clause is present.
static bool
poly_evaluate_where_clause(SemContext * ctx, odin_grammar_node_t * proc_def_node)
{
    odin_grammar_node_t * where_node = poly_find_where_clause(proc_def_node);
    if (where_node == NULL)
        return true; // no constraint

    // The WHERE_CLAUSE node has the Expression as its child
    odin_grammar_node_t * expr = NULL;
    for (size_t i = 0; i < where_node->list.count; i++)
    {
        if (where_node->list.children[i] != NULL)
        { expr = where_node->list.children[i]; break; }
    }
    if (expr == NULL)
        return true;

    long long result = poly_eval_where_expr(ctx, expr);
    if (result == -1)
        return false; // couldn't evaluate → constraint not met
    return result != 0;
}

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
    if (ctx->poly_env_stack_depth >= ctx->poly_env_stack_capacity)
    {
        int new_cap = ctx->poly_env_stack_capacity == 0 ? 4 : ctx->poly_env_stack_capacity * 2;
        PolyEnv * tmp = realloc(ctx->poly_env_stack, (size_t)new_cap * sizeof(PolyEnv));
        assert(tmp != NULL);
        ctx->poly_env_stack = tmp;
        ctx->poly_env_stack_capacity = new_cap;
    }
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

static void poly_unify_poly_idents_in_type(
    SemContext * ctx,
    odin_grammar_node_t * param_ast,
    const TypeDescriptor * arg_td,
    PolyEnv * env
);

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

    // Collect arg types from the call site.
    // ArgumentList may contain a single comma-chained Expression or separate
    // children. Handle both by walking the argument list's children and
    // decomposing comma chains.
    TypeDescriptor const * arg_types[MAX_POLY_ENV_ENTRIES];
    int arg_count = 0;
    if (arg_list_node && arg_list_node->type == AST_NODE_ARGUMENT_LIST)
    {
        // Collect the direct children of the ArgumentList
        odin_grammar_node_t * raw_args[MAX_POLY_ENV_ENTRIES];
        int raw_count = 0;
        for (size_t ai = 0; ai < arg_list_node->list.count && raw_count < MAX_POLY_ENV_ENTRIES; ai++)
        {
            if (arg_list_node->list.children[ai])
                raw_args[raw_count++] = arg_list_node->list.children[ai];
        }
        // Decompose comma chains from each child
        for (int ai = 0; ai < raw_count && arg_count < MAX_POLY_ENV_ENTRIES; ai++)
        {
            odin_grammar_node_t * chain_args[MAX_POLY_ENV_ENTRIES];
            int chain_count = 0;
            sem_collect_comma_chain_args(raw_args[ai], chain_args, MAX_POLY_ENV_ENTRIES, &chain_count);
            for (int ci = 0; ci < chain_count && arg_count < MAX_POLY_ENV_ENTRIES; ci++)
            {
                arg_types[arg_count] = chain_args[ci] ? chain_args[ci]->resolved_type : NULL;
                arg_count++;
            }
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

        // Determine if this param has a poly ident as its NAME (i.e. $T: typeid)
        // vs. in its TYPE position (x: $T). The first Identifier-like child is the name.
        odin_grammar_node_t * first_name_child = NULL;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child && (child->type == AST_NODE_IDENTIFIER || child->type == AST_NODE_POLY_IDENT))
            {
                first_name_child = child;
                break;
            }
        }
        bool is_poly_name = (first_name_child && first_name_child->type == AST_NODE_POLY_IDENT);

        // Find the type node for this param and check if its type is a poly ident reference
        odin_grammar_node_t * poly_type_node = NULL;
        bool has_poly_decl = false;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_POLY_IDENT)
            {
                char const * pn = child->text;
                if (pn && pn[0] == '$')
                    pn = pn + 1;
                if (pn && out_env->count < MAX_POLY_ENV_ENTRIES)
                {
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
                        int idx = out_env->count;
                        out_env->entries[idx].name = strdup(pn);
                        out_env->entries[idx].kind = POLY_SLOT_TYPE;
                        // If $T is in the TYPE position (x: $T), bind immediately
                        if (!is_poly_name && param_idx < arg_count)
                            out_env->entries[idx].bound_type = arg_types[param_idx];
                        else
                            out_env->entries[idx].bound_type = NULL;
                        out_env->count++;
                    }
                }
                poly_type_node = child;
                has_poly_decl = true;
            }
        }

        if (has_poly_decl && is_poly_name)
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

        // Walk the param's type subtree for POLY_IDENT references (e.g., $N
        // in array sizes like [$N]int). When found, bind from the corresponding
        // position in the arg type descriptor.
        // Skip the param name (first Identifier child) and find the type AST.
        {
            odin_grammar_node_t * param_type_ast = NULL;
            int type_ident_count = 0;
            for (size_t ci = 0; ci < param->list.count; ci++)
            {
                odin_grammar_node_t * child = param->list.children[ci];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER)
                {
                    type_ident_count++;
                    if (type_ident_count <= 1)
                        continue;
                }
                if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
                {
                    param_type_ast = child;
                    break;
                }
            }
            if (param_type_ast && param_idx < arg_count && arg_types[param_idx])
            {
                // Recursively walk the type AST to find POLY_IDENT nodes
                // that need binding from the concrete arg type.
                poly_unify_poly_idents_in_type(ctx, param_type_ast,
                    arg_types[param_idx], out_env);
            }
        }

        param_idx++;
    }

    return out_env->count > 0;
}

// Recursively walk a parameter's type AST, matching structure against a
// concrete arg type descriptor. When an AST_NODE_POLY_IDENT is found (e.g.,
// $N in [$N]int), bind it from the corresponding field of the arg type.
static void
poly_unify_poly_idents_in_type(
    SemContext * ctx,
    odin_grammar_node_t * param_ast,
    TypeDescriptor const * arg_td,
    PolyEnv * env
)
{
    if (param_ast == NULL || arg_td == NULL)
        return;

    if (param_ast->type == AST_NODE_ARRAY_TYPE && arg_td->kind == TD_KIND_ARRAY)
    {
        // Walk children: find POLY_IDENT (array size) and recurse into
        // element type.
        for (size_t i = 0; i < param_ast->list.count; i++)
        {
            odin_grammar_node_t * child = param_ast->list.children[i];
            if (child == NULL)
                continue;

            if (child->type == AST_NODE_POLY_IDENT)
            {
                // $N in array size position — bind from arg's array count
                char const * name = child->text;
                if (name == NULL)
                    continue;
                if (name[0] == '$')
                    name++;

                bool already = false;
                for (int ei = 0; ei < env->count; ei++)
                {
                    if (strcmp(env->entries[ei].name, name) == 0)
                    {
                        already = true;
                        // Fill in a placeholder if this is an int slot with value 0
                        if (env->entries[ei].kind == POLY_SLOT_INT
                            && env->entries[ei].bound_int_value == 0
                            && arg_td->as.array.count > 0)
                        {
                            env->entries[ei].bound_int_value =
                                (long long)arg_td->as.array.count;
                        }
                        break;
                    }
                }
                if (!already && env->count < MAX_POLY_ENV_ENTRIES)
                {
                    env->entries[env->count].name = strdup(name);
                    env->entries[env->count].kind = POLY_SLOT_INT;
                    env->entries[env->count].bound_int_value =
                        (long long)arg_td->as.array.count;
                    env->count++;
                }
            }
            else if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
            {
                // Recurse into element type (matches the structure of the
                // concrete arg's element type)
                poly_unify_poly_idents_in_type(
                    ctx, child, arg_td->element_type, env);
            }
        }
    }
    // Other composite types (slice, dynamic_array, etc.) could be extended
    // here in the future to handle $N in their size/length fields.
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
// Stage 5: Specialization cache
// =========================================================================

typedef struct {
    char * mangled_name;
    PolySpecialization * spec;
} PolyCacheEntry;

static PolyCacheEntry * poly_cache = NULL;
static int poly_cache_count = 0;
static int poly_cache_capacity = 0;

static PolySpecialization *
poly_cache_lookup(char const * mangled_name)
{
    if (mangled_name == NULL)
        return NULL;
    for (int i = 0; i < poly_cache_count; i++)
    {
        if (strcmp(poly_cache[i].mangled_name, mangled_name) == 0)
            return poly_cache[i].spec;
    }
    return NULL;
}

static void
poly_cache_store(char const * mangled_name, PolySpecialization * spec)
{
    if (mangled_name == NULL || spec == NULL)
        return;
    // Avoid duplicates
    if (poly_cache_lookup(mangled_name))
        return;
    if (poly_cache_count >= poly_cache_capacity)
    {
        int new_cap = poly_cache_capacity == 0 ? 8 : poly_cache_capacity * 2;
        PolyCacheEntry * tmp = realloc(poly_cache, (size_t)new_cap * sizeof(PolyCacheEntry));
        if (tmp == NULL) return;
        poly_cache = tmp;
        poly_cache_capacity = new_cap;
    }
    poly_cache[poly_cache_count].mangled_name = strdup(mangled_name);
    poly_cache[poly_cache_count].spec = spec;
    poly_cache_count++;
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

    // --- Stage 5: Check specialization cache before analysis ---
    PolySpecialization * cached = poly_cache_lookup(mangled_name);
    if (cached)
    {
        free(mangled_name);
        return cached;
    }

    // Save previous instantiating flag (supports nested polymorphism: an inner
    // poly_resolve_call must restore the outer flag rather than blindly clearing it).
    bool prev_instantiating = ctx->currently_instantiating;

    // Push env onto stack
    poly_env_push(ctx, &env);
    ctx->currently_instantiating = true;

    // Evaluate where clause (Stage 9) — must be after env push so
    // poly_env_lookup_type is available for constraint evaluation.
    if (!poly_evaluate_where_clause(ctx, proc_def))
    {
        ctx->currently_instantiating = prev_instantiating;
        poly_env_pop(ctx);
        free(mangled_name);
        return NULL; // constraint violated — caller decides error vs skip
    }

    // Run sem_analyse_procedure_literal on the original proc definition
    sem_analyse_procedure_literal(ctx, proc_def, mangled_name);

    // Extract poly int values from the env BEFORE poly_env_pop frees the entry names.
    // We need to own copies of the names because poly_env_pop frees them.
    int env_int_count = 0;
    char * env_int_names[MAX_POLY_ENV_ENTRIES];
    long long env_int_values[MAX_POLY_ENV_ENTRIES];
    for (int ei = 0; ei < env.count; ei++)
    {
        if (env.entries[ei].kind == POLY_SLOT_INT && env.entries[ei].name != NULL)
        {
            env_int_names[env_int_count] = strdup(env.entries[ei].name);
            env_int_values[env_int_count] = env.entries[ei].bound_int_value;
            env_int_count++;
        }
    }

    // Pop env (frees the strdup'd entry names in the pushed copy) and restore flag
    ctx->currently_instantiating = prev_instantiating;
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

    // Register a new specialization symbol with the mangled name.
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

    // Create specialization record
    PolySpecialization * spec = calloc(1, sizeof(PolySpecialization));
    if (spec)
    {
        spec->symbol = spec_sym;
        spec->origin_const_decl = const_decl;

        // Save polymorphic integer contract params ($N) from the pre-pop snapshot
        spec->poly_int_count = env_int_count;
        if (env_int_count > 0)
        {
            spec->poly_int_names = malloc((size_t)env_int_count * sizeof(char *));
            spec->poly_int_values = malloc((size_t)env_int_count * sizeof(long long));
            for (int ei = 0; ei < env_int_count; ei++)
            {
                spec->poly_int_names[ei] = env_int_names[ei]; // take ownership
                spec->poly_int_values[ei] = env_int_values[ei];
            }
        }

        // Save specialization-specific param types from the concrete proc type
        if (concrete_proc_type->kind == TD_KIND_PROC)
        {
            spec->param_count = concrete_proc_type->proc_metadata.param_count;
            spec->param_types = malloc((size_t)spec->param_count * sizeof(TypeDescriptor const *));
            for (int pi = 0; pi < spec->param_count; pi++)
                spec->param_types[pi] = concrete_proc_type->proc_metadata.params[pi];
        }

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

    // Store in cache for future calls
    poly_cache_store(mangled_name, spec);

    free(mangled_name);
    return spec;
}
