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

// Walk down through single-child expression wrapper nodes until reaching the
// underlying expression/type node. Handles the PEG chainl1 wrapper hierarchy
// (Expression -> AssignExpression -> ... -> PostfixExpression) and the
// PostfixExpression-with-empty-ops case (just a bare PrimaryExpression).
static odin_grammar_node_t *
poly_unwrap_expr_chain(odin_grammar_node_t * node)
{
    while (node != NULL)
    {
        switch (node->type)
        {
        case AST_NODE_EXPRESSION:
        case AST_NODE_ASSIGN_EXPRESSION:
        case AST_NODE_TERNARY_EXPRESSION:
        case AST_NODE_OR_ELSE:
        case AST_NODE_OR_RETURN:
        case AST_NODE_RANGE_EXPRESSION:
        case AST_NODE_LOG_OR_EXPRESSION:
        case AST_NODE_LOG_AND_EXPRESSION:
        case AST_NODE_BIT_OR_EXPRESSION:
        case AST_NODE_BIT_XOR_EXPRESSION:
        case AST_NODE_BIT_AND_EXPRESSION:
        case AST_NODE_SHIFT_EXPRESSION:
        case AST_NODE_ADD_EXPRESSION:
        case AST_NODE_MUL_EXPRESSION:
        case AST_NODE_COMP_EXPRESSION:
        case AST_NODE_PRIMARY_EXPRESSION:
        case AST_NODE_UNARY_EXPRESSION:
        case AST_NODE_AUTO_CAST_EXPR:
            if (node->list.count >= 1)
                node = node->list.children[0];
            else
                return NULL;
            break;
        case AST_NODE_POSTFIX_EXPRESSION:
            // Bare value (no postfix ops): descend into the primary expression.
            if (node->list.count >= 2
                && node->list.children[1] != NULL
                && node->list.children[1]->type == AST_NODE_POSTFIX_OPS
                && node->list.children[1]->list.count == 0)
            {
                node = node->list.children[0];
                break;
            }
            return node;
        default:
            return node;
        }
    }
    return node;
}

static TypeDescriptor const *
poly_resolve_type_for_where(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

    // Unwrap single-child expression wrappers (chainl1 hierarchy) and
    // AST_NODE_TYPE_NAME to get to the underlying type node.
    node = poly_unwrap_expr_chain(node);
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

// =========================================================================
// Compile-time intrinsics in where clauses (base:intrinsics predicates)
// =========================================================================

static TypeDescriptor const *
poly_unwrap_distinct(TypeDescriptor const * td)
{
    while (td != NULL && td->kind == TD_KIND_DISTINCT && td->distinct_base_type != NULL)
        td = td->distinct_base_type;
    return td;
}

static bool
poly_basic_type_is(TypeDescriptor const * td, char const * name)
{
    td = poly_unwrap_distinct(td);
    return td != NULL && td->kind == TD_KIND_BASIC && td->as.basic.name != NULL
        && strcmp(td->as.basic.name, name) == 0;
}

static bool
poly_td_is_bool(TypeDescriptor const * td)
{
    return poly_basic_type_is(td, "bool");
}

static bool
poly_td_is_rune(TypeDescriptor const * td)
{
    return poly_basic_type_is(td, "rune");
}

static bool
poly_td_is_integer(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL || td->kind != TD_KIND_BASIC || td->as.basic.is_float || td->as.basic.width <= 1)
        return false;
    if (td->as.basic.name == NULL)
        return false;
    // Exclude non-integer special basic types that share the integer layout.
    if (strcmp(td->as.basic.name, "typeid") == 0
        || strcmp(td->as.basic.name, "string") == 0
        || strcmp(td->as.basic.name, "cstring") == 0
        || strcmp(td->as.basic.name, "any") == 0)
        return false;
    return true;
}

static bool
poly_td_is_float(TypeDescriptor const * td)
{
    return is_floating_kind(td);
}

static bool
poly_td_is_complex(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL || td->kind != TD_KIND_BASIC || td->as.basic.name == NULL)
        return false;
    return strncmp(td->as.basic.name, "complex", 7) == 0;
}

static bool
poly_td_is_quaternion(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL || td->kind != TD_KIND_BASIC || td->as.basic.name == NULL)
        return false;
    return strncmp(td->as.basic.name, "quaternion", 10) == 0;
}

static bool
poly_td_is_numeric(TypeDescriptor const * td)
{
    return poly_td_is_integer(td) || poly_td_is_float(td);
}

static bool
poly_td_is_unsigned(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL || td->kind != TD_KIND_BASIC || td->as.basic.is_float)
        return false;
    return td->as.basic.is_unsigned && td->as.basic.width > 0;
}

static bool
poly_td_is_ordered(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL)
        return false;
    if (poly_td_is_numeric(td) || poly_td_is_bool(td))
        return true;
    if (poly_basic_type_is(td, "string"))
        return true;
    return td->kind == TD_KIND_ENUM
        || td->kind == TD_KIND_POINTER
        || td->kind == TD_KIND_MULTI_POINTER;
}

static bool
poly_td_is_indexable(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL)
        return false;
    if (poly_basic_type_is(td, "string"))
        return true;
    switch (td->kind)
    {
    case TD_KIND_ARRAY:
    case TD_KIND_SLICE:
    case TD_KIND_DYNAMIC_ARRAY:
    case TD_KIND_MAP:
    case TD_KIND_VECTOR:
    case TD_KIND_MATRIX:
        return true;
    default:
        return false;
    }
}

static bool
poly_td_is_sliceable(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL)
        return false;
    switch (td->kind)
    {
    case TD_KIND_ARRAY:
    case TD_KIND_SLICE:
    case TD_KIND_DYNAMIC_ARRAY:
        return true;
    default:
        return false;
    }
}

static bool
poly_td_is_comparable(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL)
        return false;
    switch (td->kind)
    {
    case TD_KIND_BASIC:
        return td->as.basic.name != NULL && strcmp(td->as.basic.name, "void") != 0;
    case TD_KIND_POINTER:
    case TD_KIND_MULTI_POINTER:
    case TD_KIND_ENUM:
        return true;
    case TD_KIND_ARRAY:
        return poly_td_is_comparable(td->element_type);
    case TD_KIND_DISTINCT:
        return poly_td_is_comparable(td->distinct_base_type);
    default:
        return false;
    }
}

static bool
poly_td_is_has_nil(TypeDescriptor const * td)
{
    td = poly_unwrap_distinct(td);
    if (td == NULL)
        return false;
    switch (td->kind)
    {
    case TD_KIND_POINTER:
    case TD_KIND_MULTI_POINTER:
    case TD_KIND_PROC:
    case TD_KIND_SLICE:
    case TD_KIND_DYNAMIC_ARRAY:
    case TD_KIND_MAYBE:
        return true;
    default:
        return false;
    }
}

// Evaluate an `intrinsics.<name>(<type>)` predicate against a resolved type.
// Returns 1 (true), 0 (false), or -1 if the intrinsic is not a recognized
// boolean predicate or the argument could not be resolved.
static long long
poly_eval_intrinsic(char const * name, TypeDescriptor const * arg_type)
{
    if (name == NULL || arg_type == NULL)
        return -1;

    if (strcmp(name, "type_is_boolean") == 0)          return poly_td_is_bool(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_integer") == 0)          return poly_td_is_integer(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_rune") == 0)             return poly_td_is_rune(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_float") == 0)            return poly_td_is_float(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_complex") == 0)          return poly_td_is_complex(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_quaternion") == 0)       return poly_td_is_quaternion(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_typeid") == 0)           return poly_basic_type_is(arg_type, "typeid") ? 1 : 0;
    if (strcmp(name, "type_is_any") == 0)              return poly_basic_type_is(arg_type, "any") ? 1 : 0;
    if (strcmp(name, "type_is_string") == 0)           return poly_basic_type_is(arg_type, "string") ? 1 : 0;
    if (strcmp(name, "type_is_unsigned") == 0)         return poly_td_is_unsigned(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_numeric") == 0)          return poly_td_is_numeric(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_ordered") == 0)          return poly_td_is_ordered(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_ordered_numeric") == 0)  return poly_td_is_numeric(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_indexable") == 0)        return poly_td_is_indexable(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_sliceable") == 0)        return poly_td_is_sliceable(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_comparable") == 0)       return poly_td_is_comparable(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_pointer") == 0)          return (arg_type->kind == TD_KIND_POINTER) ? 1 : 0;
    if (strcmp(name, "type_is_multi_pointer") == 0)    return (arg_type->kind == TD_KIND_MULTI_POINTER) ? 1 : 0;
    if (strcmp(name, "type_is_array") == 0)            return (arg_type->kind == TD_KIND_ARRAY) ? 1 : 0;
    if (strcmp(name, "type_is_slice") == 0)            return (arg_type->kind == TD_KIND_SLICE) ? 1 : 0;
    if (strcmp(name, "type_is_dynamic_array") == 0)    return (arg_type->kind == TD_KIND_DYNAMIC_ARRAY) ? 1 : 0;
    if (strcmp(name, "type_is_map") == 0)              return (arg_type->kind == TD_KIND_MAP) ? 1 : 0;
    if (strcmp(name, "type_is_struct") == 0)           return (arg_type->kind == TD_KIND_STRUCT || arg_type->kind == TD_KIND_SOA) ? 1 : 0;
    if (strcmp(name, "type_is_union") == 0)            return (arg_type->kind == TD_KIND_UNION) ? 1 : 0;
    if (strcmp(name, "type_is_enum") == 0)             return (arg_type->kind == TD_KIND_ENUM) ? 1 : 0;
    if (strcmp(name, "type_is_proc") == 0)             return (arg_type->kind == TD_KIND_PROC) ? 1 : 0;
    if (strcmp(name, "type_is_bit_set") == 0)          return (arg_type->kind == TD_KIND_BIT_SET) ? 1 : 0;
    if (strcmp(name, "type_is_bit_field") == 0)        return (arg_type->kind == TD_KIND_BIT_FIELD) ? 1 : 0;
    if (strcmp(name, "type_is_simd_vector") == 0)      return (arg_type->kind == TD_KIND_VECTOR) ? 1 : 0;
    if (strcmp(name, "type_is_matrix") == 0)           return (arg_type->kind == TD_KIND_MATRIX) ? 1 : 0;
    if (strcmp(name, "type_is_has_nil") == 0)          return poly_td_is_has_nil(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_string16") == 0)         return poly_basic_type_is(arg_type, "string16") ? 1 : 0;
    if (strcmp(name, "type_is_cstring") == 0)          return poly_basic_type_is(arg_type, "cstring") ? 1 : 0;
    if (strcmp(name, "type_is_cstring16") == 0)       return poly_basic_type_is(arg_type, "cstring16") ? 1 : 0;
    if (strcmp(name, "type_is_endian_platform") == 0)  return 1;  // We're always on the platform's native endian
    if (strcmp(name, "type_is_endian_little") == 0)    return 1;  // x86_64 is little-endian
    if (strcmp(name, "type_is_endian_big") == 0)       return 0;  // x86_64 is little-endian
    if (strcmp(name, "type_is_valid_map_key") == 0)    return poly_td_is_comparable(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_valid_matrix_elements") == 0) return poly_td_is_numeric(arg_type) ? 1 : 0;
    if (strcmp(name, "type_is_named") == 0)            return (arg_type->kind == TD_KIND_BASIC && arg_type->as.basic.name != NULL) ? 1 : 0;

    // Type-returning intrinsics: return the resulting descriptor's type_id so
    // they compose with `typeid_of(...) == typeid_of(...)` in where clauses.
    if (strcmp(name, "type_base_type") == 0 || strcmp(name, "type_core_type") == 0)
    {
        TypeDescriptor const * base = poly_unwrap_distinct(arg_type);
        return base ? base->type_id : -1;
    }
    if (strcmp(name, "type_elem_type") == 0)
    {
        TypeDescriptor const * elem = NULL;
        switch (arg_type->kind)
        {
        case TD_KIND_ARRAY:
        case TD_KIND_SLICE:
        case TD_KIND_DYNAMIC_ARRAY:
        case TD_KIND_VECTOR:
        case TD_KIND_MATRIX:
            elem = arg_type->element_type;
            break;
        case TD_KIND_POINTER:
        case TD_KIND_MULTI_POINTER:
            elem = arg_type->pointee;
            break;
        case TD_KIND_MAYBE:
            elem = arg_type->element_type;
            break;
        case TD_KIND_MAP:
            elem = arg_type->as.map.value_type;
            break;
        default:
            break;
        }
        return elem ? elem->type_id : -1;
    }

    return -1;
}

// Evaluate `intrinsics.<name>(<type>)` or `<alias>(<type>)` inside a where
// clause. The node is a POSTFIX_EXPRESSION: [base, PostfixOps[member?, call]].
// Handles both the package-qualified form (`intrinsics.type_is_float($T)`) and
// the constant-alias form (`IS_FLOAT($T)`), following chained aliases.
static long long
poly_eval_where_call(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->type != AST_NODE_POSTFIX_EXPRESSION || node->list.count < 2)
        return -1;

    odin_grammar_node_t * ops = node->list.children[1];
    if (ops == NULL || ops->type != AST_NODE_POSTFIX_OPS || ops->list.count == 0)
        return -1;

    char const * member_name = NULL;
    odin_grammar_node_t * call_op = NULL;
    for (size_t i = 0; i < ops->list.count; i++)
    {
        odin_grammar_node_t * op = ops->list.children[i];
        if (op == NULL)
            continue;
        if (op->type == AST_NODE_POSTFIX_MEMBER && op->list.count >= 1 && op->list.children[0] != NULL)
            member_name = op->list.children[0]->text;
        else if (op->type == AST_NODE_POSTFIX_CALL)
            call_op = op;
    }
    if (call_op == NULL)
        return -1;

    // Resolve the base identifier.
    odin_grammar_node_t * base = node->list.children[0];
    odin_grammar_node_t * inner = base;
    while (inner != NULL && inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
        inner = inner->list.children[0];

    char const * intrinsic_name = NULL;

    if (inner != NULL && inner->type == AST_NODE_IDENTIFIER && member_name != NULL)
    {
        // Package-qualified form: intrinsics.type_is_float
        ImportedPackage * pkg = find_imported_package_by_name(ctx, inner->text);
        if (pkg != NULL
            && strcmp(pkg->package_name ? pkg->package_name : "", "intrinsics") == 0
            && scope_find_symbol_entry(pkg->package_scope, member_name) != NULL)
        {
            intrinsic_name = member_name;
        }
    }

    if (intrinsic_name == NULL && inner != NULL && inner->type == AST_NODE_IDENTIFIER)
    {
        // Bare identifier: a constant alias like IS_FLOAT. Follow the alias
        // chain to the underlying intrinsic proc symbol.
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), inner->text);
        if (sym != NULL)
        {
            symbol_t * intrinsic = poly_get_intrinsic_alias(sym);
            while (intrinsic != NULL && poly_get_intrinsic_alias(intrinsic) != NULL)
                intrinsic = poly_get_intrinsic_alias(intrinsic);
            if (intrinsic != NULL && intrinsic->name != NULL)
                intrinsic_name = intrinsic->name;
        }
    }

    if (intrinsic_name == NULL)
        return -1;

    // Extract the single type argument.
    if (call_op->list.count < 1 || call_op->list.children[0] == NULL)
        return -1;
    odin_grammar_node_t * arg_list = call_op->list.children[0];
    odin_grammar_node_t * arg = NULL;
    if (arg_list->type == AST_NODE_ARGUMENT_LIST && arg_list->list.count >= 1)
        arg = arg_list->list.children[0];
    if (arg == NULL)
        return -1;

    TypeDescriptor const * arg_type = poly_resolve_type_for_where(ctx, arg);
    if (arg_type == NULL)
        return -1;

    return poly_eval_intrinsic(intrinsic_name, arg_type);
}

// Evaluate a where-clause sub-expression, returning a concrete value.
// Returns -1 if the expression cannot be evaluated at compile time.
// Exposed via polymorphism.h for compile-time `when` condition reuse.
long long
poly_eval_where_expr(SemContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return -1;

    // Unwrap single-child expression wrappers
    while (node != NULL && node->list.children[0] != NULL
           && node->type != AST_NODE_TYPEID_OF_EXPR
           && node->type != AST_NODE_SIZE_OF_EXPR
           && node->type != AST_NODE_INTEGER_VALUE
           && node->type != AST_NODE_BOOL_TRUE
           && node->type != AST_NODE_BOOL_FALSE
           && node->type != AST_NODE_IDENTIFIER)
    {
        if (node->list.count == 1)
        {
            node = node->list.children[0];
        }
        else if (node->type == AST_NODE_POSTFIX_EXPRESSION && node->list.count >= 2
                 && node->list.children[1] != NULL
                 && node->list.children[1]->type == AST_NODE_POSTFIX_OPS
                 && node->list.children[1]->list.count == 0)
        {
            // Bare primary expression with no postfix ops: descend into base
            node = node->list.children[0];
        }
        else
        {
            break;
        }
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

    case AST_NODE_POSTFIX_EXPRESSION:
        return poly_eval_where_call(ctx, node);

    case AST_NODE_IDENTIFIER:
    {
        // Check poly int env (for $N params)
        if (node->text != NULL)
        {
            long long val = 0;
            if (poly_env_lookup_int(ctx, node->text, &val))
                return val;
            // Fall back to poly type env: a bare type name resolves to its
            // type_id so `type_elem_type(T) == E` style constraints work.
            TypeDescriptor const * td = poly_env_lookup_type(ctx, node->text);
            if (td != NULL)
                return td->type_id;
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

    // The where expression is a comma-separated chain of constraints
    // (Expression = chainl1(AssignExpression, Comma)); each must hold.
    odin_grammar_node_t * constraints[128];
    int constraint_count = 0;
    sem_collect_comma_chain_args(expr, constraints, 128, &constraint_count);
    if (constraint_count == 0)
        return true;

    for (int ci = 0; ci < constraint_count; ci++)
    {
        long long result = poly_eval_where_expr(ctx, constraints[ci]);
        if (result == -1)
            return false; // couldn't evaluate → constraint not met
        if (result == 0)
            return false; // violated
    }
    return true;
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

bool
poly_struct_has_type_params(odin_grammar_node_t const * struct_node)
{
    if (struct_node == NULL)
        return false;

    // StructType children: ParameterList? (Directive IntegerLiteral?)? StructRawBody
    // Look for a ParameterList child
    for (size_t i = 0; i < struct_node->list.count; i++)
    {
        odin_grammar_node_t * child = struct_node->list.children[i];
        if (child != NULL && child->type == AST_NODE_PARAMETER_LIST)
        {
            // Check if any parameter has a PolyIdent in its type
            for (size_t j = 0; j < child->list.count; j++)
            {
                odin_grammar_node_t * param = child->list.children[j];
                if (param != NULL && poly_walk_has_ident(param))
                    return true;
            }
        }
    }
    return false;
}

bool
poly_type_has_type_params(odin_grammar_node_t const * type_node)
{
    if (type_node == NULL)
        return false;

    // Check if this type node has a ParameterList child (for poly enum/union)
    for (size_t i = 0; i < type_node->list.count; i++)
    {
        odin_grammar_node_t * child = type_node->list.children[i];
        if (child != NULL && child->type == AST_NODE_PARAMETER_LIST)
        {
            // Check if any parameter has a PolyIdent in its type
            for (size_t j = 0; j < child->list.count; j++)
            {
                odin_grammar_node_t * param = child->list.children[j];
                if (param != NULL && poly_walk_has_ident(param))
                    return true;
            }
        }
    }
    return false;
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
// Intrinsic-alias side table (symbol_t* alias -> symbol_t* intrinsic proc)
// Mirrors the poly_origin side table pattern. Aliases are constants like
// `@private IS_FLOAT :: intrinsics.type_is_float`.
// =========================================================================

typedef struct
{
    symbol_t * alias_sym;
    symbol_t * intrinsic_sym;
} PolyIntrinsicAliasEntry;

static PolyIntrinsicAliasEntry * poly_intrinsic_aliases = NULL;
static int poly_intrinsic_alias_count = 0;
static int poly_intrinsic_alias_capacity = 0;

void
poly_register_intrinsic_alias(symbol_t * alias_sym, symbol_t * intrinsic_sym)
{
    if (alias_sym == NULL || intrinsic_sym == NULL)
        return;
    for (int i = 0; i < poly_intrinsic_alias_count; i++)
    {
        if (poly_intrinsic_aliases[i].alias_sym == alias_sym)
        {
            poly_intrinsic_aliases[i].intrinsic_sym = intrinsic_sym;
            return;
        }
    }
    if (poly_intrinsic_alias_count >= poly_intrinsic_alias_capacity)
    {
        int new_cap = poly_intrinsic_alias_capacity == 0 ? 8 : poly_intrinsic_alias_capacity * 2;
        PolyIntrinsicAliasEntry * tmp = realloc(
            poly_intrinsic_aliases, (size_t)new_cap * sizeof(PolyIntrinsicAliasEntry));
        if (tmp == NULL)
            return;
        poly_intrinsic_aliases = tmp;
        poly_intrinsic_alias_capacity = new_cap;
    }
    poly_intrinsic_aliases[poly_intrinsic_alias_count].alias_sym = alias_sym;
    poly_intrinsic_aliases[poly_intrinsic_alias_count].intrinsic_sym = intrinsic_sym;
    poly_intrinsic_alias_count++;
}

symbol_t *
poly_get_intrinsic_alias(symbol_t * alias_sym)
{
    if (alias_sym == NULL)
        return NULL;
    for (int i = 0; i < poly_intrinsic_alias_count; i++)
    {
        if (poly_intrinsic_aliases[i].alias_sym == alias_sym)
            return poly_intrinsic_aliases[i].intrinsic_sym;
    }
    return NULL;
}

// Side table mapping a WHEN_STATEMENT AST node to the body the semantic
// analyser selected at compile time. Stored out-of-band (NOT in node->metadata,
// which the AST teardown free()s) so the IR generator can reuse the selection.
typedef struct
{
    odin_grammar_node_t * when_node;
    odin_grammar_node_t * selected_body;
} PolyWhenSelectionEntry;

static PolyWhenSelectionEntry * poly_when_selections = NULL;
static int poly_when_selection_count = 0;
static int poly_when_selection_capacity = 0;

void
poly_register_when_selection(odin_grammar_node_t * when_node, odin_grammar_node_t * body)
{
    if (when_node == NULL)
        return;
    for (int i = 0; i < poly_when_selection_count; i++)
    {
        if (poly_when_selections[i].when_node == when_node)
        {
            poly_when_selections[i].selected_body = body;
            return;
        }
    }
    if (poly_when_selection_count >= poly_when_selection_capacity)
    {
        int new_cap = poly_when_selection_capacity == 0 ? 8 : poly_when_selection_capacity * 2;
        PolyWhenSelectionEntry * tmp = realloc(
            poly_when_selections, (size_t)new_cap * sizeof(PolyWhenSelectionEntry));
        if (tmp == NULL)
            return;
        poly_when_selections = tmp;
        poly_when_selection_capacity = new_cap;
    }
    poly_when_selections[poly_when_selection_count].when_node = when_node;
    poly_when_selections[poly_when_selection_count].selected_body = body;
    poly_when_selection_count++;
}

odin_grammar_node_t *
poly_get_when_selection(odin_grammar_node_t * when_node)
{
    if (when_node == NULL)
        return NULL;
    for (int i = 0; i < poly_when_selection_count; i++)
    {
        if (poly_when_selections[i].when_node == when_node)
            return poly_when_selections[i].selected_body;
    }
    return NULL;
}

// True if `name` is one of the compile-time type-query intrinsics supported by
// the where-clause evaluator (matches stubs/base/intrinsics/intrinsics.odin).
bool
poly_is_known_intrinsic_name(char const * name)
{
    if (name == NULL)
        return false;
    return strcmp(name, "type_base_type") == 0
        || strcmp(name, "type_core_type") == 0
        || strcmp(name, "type_elem_type") == 0
        || strcmp(name, "type_is_boolean") == 0
        || strcmp(name, "type_is_integer") == 0
        || strcmp(name, "type_is_rune") == 0
        || strcmp(name, "type_is_float") == 0
        || strcmp(name, "type_is_complex") == 0
        || strcmp(name, "type_is_quaternion") == 0
        || strcmp(name, "type_is_typeid") == 0
        || strcmp(name, "type_is_any") == 0
        || strcmp(name, "type_is_string") == 0
        || strcmp(name, "type_is_unsigned") == 0
        || strcmp(name, "type_is_numeric") == 0
        || strcmp(name, "type_is_ordered") == 0
        || strcmp(name, "type_is_ordered_numeric") == 0
        || strcmp(name, "type_is_indexable") == 0
        || strcmp(name, "type_is_sliceable") == 0
        || strcmp(name, "type_is_comparable") == 0
        || strcmp(name, "type_is_pointer") == 0
        || strcmp(name, "type_is_multi_pointer") == 0
        || strcmp(name, "type_is_array") == 0
        || strcmp(name, "type_is_slice") == 0
        || strcmp(name, "type_is_dynamic_array") == 0
        || strcmp(name, "type_is_map") == 0
        || strcmp(name, "type_is_struct") == 0
        || strcmp(name, "type_is_union") == 0
        || strcmp(name, "type_is_enum") == 0
        || strcmp(name, "type_is_proc") == 0
        || strcmp(name, "type_is_bit_set") == 0
        || strcmp(name, "type_is_bit_field") == 0
        || strcmp(name, "type_is_simd_vector") == 0
        || strcmp(name, "type_is_matrix") == 0
        || strcmp(name, "type_is_has_nil") == 0;
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

static bool poly_unify_poly_idents_in_type(
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

        // Extract names + type node (handles multi-name params: "a, b: $T").
        odin_grammar_node_t * param_names[32];
        odin_grammar_node_t * type_node = NULL;
        bool is_poly_name = false;
        int name_count = sem_extract_param_names(
            param, param_names, 32, &type_node, &is_poly_name);

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
            // (it's a compile-time construct). The scan above already registered
            // the $T name (unbound); a later "x: T" param binds it from its arg.
            continue;
        }

        if (name_count == 0 || type_node == NULL)
            continue;

        // For non-poly params (x: T), if the type references a poly param
        // name (a bare Identifier or an Identifier inside the type subtree),
        // bind that poly name to this arg's type.
        if (type_node != NULL && param_idx < arg_count)
        {
            odin_grammar_node_t * found = NULL;
            if (type_node->type == AST_NODE_IDENTIFIER)
                found = type_node;
            else if (is_type_node(type_node))
                found = poly_find_ident_in_subtree(type_node);
            if (found && found->type == AST_NODE_IDENTIFIER)
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

        // Walk the param's type subtree for POLY_IDENT references (e.g., $N
        // in array sizes like [$N]int). When found, bind from the corresponding
        // position in the arg type descriptor.
        if (type_node != NULL && param_idx < arg_count && arg_types[param_idx])
        {
            // Recursively walk the type AST to find POLY_IDENT nodes
            // that need binding from the concrete arg type.
            (void)poly_unify_poly_idents_in_type(ctx, type_node,
                arg_types[param_idx], out_env);
        }

        // Multi-name params consume one arg slot per name.
        param_idx += name_count;
    }

    return out_env->count > 0;
}

// Recursively walk a parameter's type AST, matching structure against a
// concrete arg type descriptor. When an AST_NODE_POLY_IDENT is found (e.g.,
// $N in [$N]int), bind it from the corresponding field of the arg type.

// Bind a poly ident name to a type in the env.
static void
poly_env_bind_type(PolyEnv * env, char const * name, TypeDescriptor const * td)
{
    bool already = false;
    for (int ei = 0; ei < env->count; ei++)
    {
        if (strcmp(env->entries[ei].name, name) == 0)
        {
            already = true;
            if (env->entries[ei].kind == POLY_SLOT_TYPE
                && env->entries[ei].bound_type == NULL)
                env->entries[ei].bound_type = td;
            break;
        }
    }
    if (!already && env->count < MAX_POLY_ENV_ENTRIES)
    {
        env->entries[env->count].name = strdup(name);
        env->entries[env->count].kind = POLY_SLOT_TYPE;
        env->entries[env->count].bound_type = td;
        env->count++;
    }
}

// Bind a poly ident name to an integer in the env.
static void
poly_env_bind_int(PolyEnv * env, char const * name, long long val)
{
    bool already = false;
    for (int ei = 0; ei < env->count; ei++)
    {
        if (strcmp(env->entries[ei].name, name) == 0)
        {
            already = true;
            if (env->entries[ei].kind == POLY_SLOT_INT
                && env->entries[ei].bound_int_value == 0 && val > 0)
                env->entries[ei].bound_int_value = val;
            break;
        }
    }
    if (!already && env->count < MAX_POLY_ENV_ENTRIES)
    {
        env->entries[env->count].name = strdup(name);
        env->entries[env->count].kind = POLY_SLOT_INT;
        env->entries[env->count].bound_int_value = val;
        env->count++;
    }
}

// Strip leading '$' from a name and bind a POLY_IDENT node to a type.
static void
poly_bind_poly_ident_type(odin_grammar_node_t * node,
                          TypeDescriptor const * td, PolyEnv * env)
{
    char const * name = node->text;
    if (name == NULL) return;
    if (name[0] == '$') name++;
    poly_env_bind_type(env, name, td);
}

// Scan children of a type AST node for POLY_IDENTs and recurse into
// nested type nodes, binding against the given element type.
// Returns true if any poly ident was bound or type recursion succeeded.
static bool
poly_scan_children_for_poly_idents(
    odin_grammar_node_t * param_ast,
    TypeDescriptor const * elem_td,
    PolyEnv * env)
{
    bool any = false;
    for (size_t i = 0; i < param_ast->list.count; i++)
    {
        odin_grammar_node_t * child = param_ast->list.children[i];
        if (child == NULL) continue;
        if (child->type == AST_NODE_POLY_IDENT)
        {
            poly_bind_poly_ident_type(child, elem_td, env);
            any = true;
        }
        else if (is_type_node(child))
        {
            if (poly_unify_poly_idents_in_type(NULL, child, elem_td, env))
                any = true;
        }
    }
    return any;
}

// Walk an expression chain to extract a compile-time integer value.
// Used to compare concrete matrix dimensions; avoids SemContext dependency.
static long long
poly_extract_matrix_dim(odin_grammar_node_t * child)
{
    while (child != NULL)
    {
        if (child->type == AST_NODE_INTEGER_VALUE && child->text != NULL)
            return atoll(child->text);
        // Unwrap single-child expression wrappers
        if (child->list.count == 1 && child->list.children[0] != NULL)
            child = child->list.children[0];
        else if (child->type == AST_NODE_POSTFIX_EXPRESSION && child->list.count >= 1)
            child = child->list.children[0];
        else
            break;
    }
    return 0;
}

static bool
poly_unify_poly_idents_in_type(
    SemContext * ctx,
    odin_grammar_node_t * param_ast,
    TypeDescriptor const * arg_td,
    PolyEnv * env
)
{
    if (param_ast == NULL || arg_td == NULL)
        return false;

    if (param_ast->type == AST_NODE_ARRAY_TYPE
        && (arg_td->kind == TD_KIND_ARRAY || arg_td->kind == TD_KIND_VECTOR))
    {
        // Walk children: find POLY_IDENTs and recurse into type nodes.
        // For [$N]$T: first POLY_IDENT is size (bind int), remaining
        // type subtree contains the element type. Accepts both array and
        // #simd vector args (binds $N = lane_count, $E = element type).
        long long count;
        TypeDescriptor const * elem_td;
        if (arg_td->kind == TD_KIND_ARRAY)
        {
            count = (long long)arg_td->as.array.count;
            elem_td = arg_td->element_type;
        }
        else
        {
            count = (long long)arg_td->as.vector.lane_count;
            elem_td = arg_td->as.vector.element_type;
        }

        bool any = false;
        for (size_t i = 0; i < param_ast->list.count; i++)
        {
            odin_grammar_node_t * child = param_ast->list.children[i];
            if (child == NULL) continue;
            if (child->type == AST_NODE_POLY_IDENT)
            {
                char const * raw = child->text;
                if (raw == NULL) continue;
                char const * name = (raw[0] == '$') ? raw + 1 : raw;

                // Check if already bound as int (from a previous pass)
                bool already_bound = false;
                for (int ei = 0; ei < env->count; ei++)
                {
                    if (strcmp(env->entries[ei].name, name) == 0)
                    {
                        already_bound = true;
                        if (env->entries[ei].kind == POLY_SLOT_INT
                            && env->entries[ei].bound_int_value == 0
                            && count > 0)
                            env->entries[ei].bound_int_value = count;
                        break;
                    }
                }
                if (!already_bound)
                {
                    // First encounter of this POLY_IDENT in the array.
                    // Heuristic: if it's the first child and there are
                    // more children, it's the size position (bind int).
                    // Otherwise, it's the element type (bind type).
                    if (i == 0 && param_ast->list.count > 1)
                    {
                        poly_env_bind_int(env, name, count);
                        any = true;
                    }
                    else if (elem_td != NULL)
                    {
                        poly_env_bind_type(env, name, elem_td);
                        any = true;
                    }
                }
            }
            else if (is_type_node(child) || child->type == AST_NODE_IDENTIFIER)
            {
                if (poly_unify_poly_idents_in_type(
                        ctx, child, elem_td, env))
                    any = true;
            }
        }
        return any;
    }
    // Slice: []$T
    if (param_ast->type == AST_NODE_SLICE_TYPE && arg_td->kind == TD_KIND_SLICE)
    {
        return poly_scan_children_for_poly_idents(param_ast, arg_td->element_type, env);
    }
    // Dynamic array: [dynamic]$T
    if (param_ast->type == AST_NODE_DYNAMIC_ARRAY_TYPE && arg_td->kind == TD_KIND_DYNAMIC_ARRAY)
    {
        return poly_scan_children_for_poly_idents(param_ast, arg_td->element_type, env);
    }
    // Multi-pointer: [|^]$T
    if (param_ast->type == AST_NODE_MULTI_POINTER_TYPE && arg_td->kind == TD_KIND_MULTI_POINTER)
    {
        return poly_scan_children_for_poly_idents(param_ast, arg_td->element_type, env);
    }
    // Pointer: ^$T
    if (param_ast->type == AST_NODE_POINTER_TYPE && arg_td->kind == TD_KIND_POINTER)
    {
        return poly_scan_children_for_poly_idents(param_ast, arg_td->pointee, env);
    }
    // Maybe: Maybe($T)
    if (param_ast->type == AST_NODE_MAYBE_TYPE && arg_td->kind == TD_KIND_MAYBE)
    {
        return poly_scan_children_for_poly_idents(param_ast, arg_td->element_type, env);
    }
    // SpecType: $M/[pattern] — bind $M to full concrete type ONLY when
    // the pattern matches the arg type structure.
    if (param_ast->type == AST_NODE_SPEC_TYPE)
    {
        // Children: [PolyIdent($M), SpecOperator, PatternType]
        odin_grammar_node_t * poly_ident = (param_ast->list.count >= 1)
            ? param_ast->list.children[0] : NULL;
        odin_grammar_node_t * pattern = (param_ast->list.count >= 3)
            ? param_ast->list.children[2] : NULL;
        if (pattern)
        {
            // Recurse into pattern first. Only bind $M if the pattern
            // structurally matches the arg type (returned true).
            if (poly_unify_poly_idents_in_type(ctx, pattern, arg_td, env))
            {
                if (poly_ident && poly_ident->type == AST_NODE_POLY_IDENT)
                    poly_bind_poly_ident_type(poly_ident, arg_td, env);
                return true;
            }
        }
        return false;
    }
    // Matrix: matrix[2, 2]$T
    if (param_ast->type == AST_NODE_MATRIX_TYPE && arg_td->kind == TD_KIND_MATRIX)
    {
        // MatrixType children: [rows_Expr, columns_Expr, ElementType]
        // Check concrete (IntegerLiteral) dimensions match the arg type.
        // If rows/cols are POLY_IDENTs ($N, $M), they'll be bound below.
        bool dims_match = true;
        if (param_ast->list.count >= 2)
        {
            odin_grammar_node_t * rows_child = param_ast->list.children[0];
            odin_grammar_node_t * cols_child = param_ast->list.children[1];
            if (rows_child)
            {
                long long expected_rows = poly_extract_matrix_dim(rows_child);
                if (expected_rows > 0 && expected_rows != arg_td->as.matrix.rows)
                    dims_match = false;
            }
            if (cols_child)
            {
                long long expected_cols = poly_extract_matrix_dim(cols_child);
                if (expected_cols > 0 && expected_cols != arg_td->as.matrix.columns)
                    dims_match = false;
            }
        }
        if (dims_match)
            return poly_scan_children_for_poly_idents(param_ast, arg_td->element_type, env);
        return false;
    }
    // Other composite types could be extended here in the future.
    return false;
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
// Stage 12: Return-position poly-binding fallback
//
// When a poly proc has `$T` in the return position but NOT in any
// parameter position, `poly_build_env_from_args` cannot derive a binding
// from args. The surrounding context (e.g. `r: int = poly_call()`) sets
// `ctx->poly_expected_return_type` before invoking the call, and this
// helper walks the proc's AST_NODE_RETURNS subtree to find every
// `AST_NODE_POLY_IDENT` and bind it to that expected type.
// =========================================================================

// Walk the RETURNS subtree and collect distinct poly-ident names.
// `seen_names` / `seen_count` deduplicates within this call.
static void
poly_collect_return_poly_idents(odin_grammar_node_t const * node,
                                 char const * seen_names[MAX_POLY_ENV_ENTRIES],
                                 int * seen_count)
{
    if (node == NULL || *seen_count >= MAX_POLY_ENV_ENTRIES)
        return;

    if (node->type == AST_NODE_POLY_IDENT)
    {
        char const * name = node->text;
        if (name == NULL)
            return;
        // Strip leading '$' if present (poly idents store text like "T").
        if (name[0] == '$')
            name++;
        // Dedup
        for (int i = 0; i < *seen_count; i++)
        {
            if (strcmp(seen_names[i], name) == 0)
                return;
        }
        seen_names[(*seen_count)++] = name;
        return;
    }
    // Also pick up bare identifiers whose name matches a poly slot we
    // already created (the shorthand `proc() -> T` form, where `T` was
    // declared via `$T: typeid` in the parameter list). For the
    // return-position-only case, we only collect POLY_IDENTs here;
    // the bare-`T` form is handled by poly_env_lookup_type when it
    // resolves the type node.
    for (size_t i = 0; i < node->list.count; i++)
        poly_collect_return_poly_idents(node->list.children[i], seen_names, seen_count);
}

// Try to fill any empty slots in `env` using the return-position poly idents.
// Returns true if at least one new binding was added (i.e. the env now has
// at least one slot with a non-NULL bound_type that was previously unset).
static bool
poly_bind_from_return_type(SemContext * ctx,
                           odin_grammar_node_t * proc_def,
                           PolyEnv * env,
                           TypeDescriptor const * expected_return_type){
    if (proc_def == NULL || expected_return_type == NULL)
        return false;
    (void)ctx;

    // Find the ProcedureSignature then the AST_NODE_RETURNS inside it.
    odin_grammar_node_t * proc_sig = NULL;
    for (size_t i = 0; i < proc_def->list.count; i++)
    {
        odin_grammar_node_t * child = proc_def->list.children[i];
        if (child && child->type == AST_NODE_PROCEDURE_SIGNATURE)
        {
            proc_sig = child;
            break;
        }
    }
    if (proc_sig == NULL)
        return false;

    odin_grammar_node_t * returns_node = NULL;
    for (size_t i = 0; i < proc_sig->list.count; i++)
    {
        odin_grammar_node_t * child = proc_sig->list.children[i];
        if (child && child->type == AST_NODE_RETURNS)
        {
            returns_node = child;
            break;
        }
    }
    if (returns_node == NULL)
        return false;

    // Collect distinct poly-ident names in the return subtree.
    char const * seen_names[MAX_POLY_ENV_ENTRIES];
    int seen_count = 0;
    poly_collect_return_poly_idents(returns_node, seen_names, &seen_count);
    if (seen_count == 0)
        return false;

    // For the simple case of a single poly var, bind it to expected_return_type.
    // If there are multiple poly vars, we cannot disambiguate which one
    // corresponds to the expected type — defer (return false).
    if (seen_count != 1)
        return false;

    char const * name = seen_names[0];

    // If a slot for this name already exists with a binding, don't override.
    for (int i = 0; i < env->count; i++)
    {
        if (env->entries[i].kind == POLY_SLOT_TYPE
            && env->entries[i].name != NULL
            && strcmp(env->entries[i].name, name) == 0
            && env->entries[i].bound_type != NULL)
        {
            return false; // already bound
        }
    }

    // Create or fill a slot binding this poly name to the expected type.
    for (int i = 0; i < env->count; i++)
    {
        if (env->entries[i].kind == POLY_SLOT_TYPE
            && env->entries[i].name != NULL
            && strcmp(env->entries[i].name, name) == 0)
        {
            env->entries[i].bound_type = expected_return_type;
            return true;
        }
    }
    // No existing slot — create a new one (the name wasn't declared via
    // `$T: typeid` parameter; it only appears in the return type as `$T`).
    if (env->count >= MAX_POLY_ENV_ENTRIES)
        return false;
    env->entries[env->count].name = strdup(name);
    env->entries[env->count].kind = POLY_SLOT_TYPE;
    env->entries[env->count].bound_type = expected_return_type;
    env->count++;
    return true;
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

    // Build PolyEnv from arg types (Stage 1-8: binds $T/$N from concrete args).
    PolyEnv env;
    if (!poly_build_env_from_args(ctx, poly_symbol, proc_def, arg_list_node, &env))
    {
        // Stage 12: If the env build failed (e.g., $T appears only in the
        // return position — no parameter binding), try to bind $T from the
        // surrounding context via `ctx->poly_expected_return_type`.
        if (ctx->poly_expected_return_type != NULL
            && poly_bind_from_return_type(ctx, proc_def, &env,
                                            ctx->poly_expected_return_type))
        {
            // Fallback succeeded — env now has at least one binding.
        }
        else
        {
            sem_error_list_add(&ctx->errors, NULL, call_op,
                               "failed to build polymorphic environment from arguments");
            return NULL;
        }
    }
    else
    {
        // Stage 12: Even if poly_build_env_from_args succeeded, there may
        // be a return-position $T that wasn't bound (because no parameter
        // referenced it). Try the fallback to fill any remaining unbound
        // type slots from the expected return type.
        if (ctx->poly_expected_return_type != NULL)
            poly_bind_from_return_type(ctx, proc_def, &env,
                                         ctx->poly_expected_return_type);
    }

    // Guard: if the env is empty but the proc is polymorphic, we have no
    // bindings and cannot specialize. This catches the case of `$T` only
    // appearing in the return position with no argument-derived bindings.
    if (env.count == 0)
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
