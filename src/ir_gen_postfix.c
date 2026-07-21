#include "ir_gen_postfix.h"
#include "ast_metadata.h"
#include "ast_utils.h"

// --- Postfix expression / call codegen ---

// Evaluate a single argument expression, possibly expanding struct/array fields
// for expand_values(). Returns the number of arguments produced.
static int
ir_gen_collect_single_arg(IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef * args, TypeDescriptor const ** arg_types, int max_args)
{
    if (node == NULL || max_args <= 0)
        return 0;

    // Unwrap single-child expression wrappers to find the actual expression.
    // The chainl1 combinator with one element produces a wrapper for each
    // expression level (ASSIGN_EXPRESSION -> LOG_OR -> LOG_AND -> ...).
    // Multi-child wrappers (ADD_EXPRESSION with `-`, etc.) are NOT unwrapped.
    while (node != NULL && node->list.count == 1)
    {
        switch (node->type)
        {
        case AST_NODE_PRIMARY_EXPRESSION:
        case AST_NODE_POSTFIX_EXPRESSION:
        case AST_NODE_UNARY_EXPRESSION:
        case AST_NODE_MUL_EXPRESSION:
        case AST_NODE_ADD_EXPRESSION:
        case AST_NODE_SHIFT_EXPRESSION:
        case AST_NODE_BIT_AND_EXPRESSION:
        case AST_NODE_BIT_XOR_EXPRESSION:
        case AST_NODE_BIT_OR_EXPRESSION:
        case AST_NODE_COMP_EXPRESSION:
        case AST_NODE_LOG_AND_EXPRESSION:
        case AST_NODE_LOG_OR_EXPRESSION:
        case AST_NODE_RANGE_EXPRESSION:
        case AST_NODE_TERNARY_EXPRESSION:
        case AST_NODE_OR_ELSE:
        case AST_NODE_OR_RETURN:
        case AST_NODE_ASSIGN_EXPRESSION:
        case AST_NODE_EXPRESSION:
            node = node->list.children[0];
            continue;
        default:
            break;
        }
        break;
    }
    if (node == NULL || max_args <= 0)
        return 0;

    // ExpandValuesExpr: expand struct/array fields as individual arguments
    if (node->type == AST_NODE_EXPAND_VALUES_EXPR && node->list.count >= 1 && node->list.children[0] != NULL)
    {
        odin_grammar_node_t * inner = node->list.children[0];
        LLVMValueRef agg_val = ir_gen_node(ctx, inner);
        if (agg_val == NULL)
            return 0;

        TypeDescriptor const * agg_type = inner->resolved_type;
        if (agg_type == NULL)
            return 0;

        // Load if pointer
        if (LLVMGetTypeKind(LLVMTypeOf(agg_val)) == LLVMPointerTypeKind && agg_type->llvm_type != NULL)
        {
            agg_val = LLVMBuildLoad2(ctx->builder, agg_type->llvm_type, agg_val, "expand.load");
        }

        if (agg_type->kind == TD_KIND_STRUCT)
        {
            int field_count = agg_type->struct_metadata.members.count;
            int count = 0;
            for (int fi = 0; fi < field_count && count < max_args; fi++)
            {
                args[count] = LLVMBuildExtractValue(ctx->builder, agg_val, (unsigned)fi, "expand.field");
                if (arg_types)
                {
                    struct_field_t const * f = type_descriptor_get_struct_field(agg_type, fi);
                    arg_types[count] = f ? f->type_desc : NULL;
                }
                count++;
            }
            return count;
        }
        else if (agg_type->kind == TD_KIND_ARRAY)
        {
            int elem_count = (int)agg_type->as.array.count;
            int count = 0;
            for (int ei = 0; ei < elem_count && count < max_args; ei++)
            {
                args[count] = LLVMBuildExtractValue(ctx->builder, agg_val, (unsigned)ei, "expand.elem");
                if (arg_types)
                    arg_types[count] = agg_type->element_type;
                count++;
            }
            return count;
        }
        return 0;
    }

    // Normal single expression
    args[0] = ir_gen_node(ctx, node);
    if (arg_types)
        arg_types[0] = node->resolved_type;
    return args[0] ? 1 : 0;
}

// Walk a comma-chained Expression tree to collect individual argument values.
// Comma is a terminal (lexeme) so it produces no AST node.
// Walk a comma-chain Expression tree to collect individual argument nodes.
// Same pattern as ir_gen_collect_call_args but just collects node pointers.
void
ir_gen_collect_comma_chain_args(odin_grammar_node_t * node, odin_grammar_node_t ** out_args, int max_args, int * out_count)
{
    if (node == NULL || max_args <= 0)
        return;
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
        int last_idx = (int)node->list.count - 1;
        odin_grammar_node_t * last = node->list.children[last_idx];
        ir_gen_collect_comma_chain_args(node->list.children[0], out_args, max_args, out_count);
        if (*out_count < max_args && last != NULL)
        {
            out_args[*out_count] = last;
            (*out_count)++;
        }
    }
    else
    {
        if (*out_count < max_args)
        {
            out_args[*out_count] = node;
            (*out_count)++;
        }
    }
}

// chainl1(AssignExpression, Comma) produces a left-associative tree:
//   Expr(Expr(a, b), c)   for a, b, c
static int
ir_gen_collect_call_args(IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef * args, TypeDescriptor const ** arg_types, int max_args)
{
    if (node == NULL || max_args <= 0)
        return 0;

    // Detect comma-chainl1: AST_NODE_EXPRESSION with >=2 children
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
        // children[0] is the left sub-chain, children[last] is the rightmost
        // operand
        odin_grammar_node_t * last = node->list.children[node->list.count - 1];
        int count = ir_gen_collect_call_args(ctx, node->list.children[0], args, arg_types, max_args);
        if (count < max_args && last != NULL)
        {
            count += ir_gen_collect_single_arg(ctx, last, args + count, arg_types ? arg_types + count : NULL, max_args - count);
        }
        return count;
    }

    // Single expression — evaluate directly
    return ir_gen_collect_single_arg(ctx, node, args, arg_types, max_args);
}

// Phase 3.3 helpers

static bool
ir_gen_postfix_call(IrGenContext * ctx, odin_grammar_node_t * node, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    TypeDescriptor const * proc_type = NULL;

    // Priority 1: semantic analyser resolved this to a concrete symbol
    // (e.g., polymorphic specialization or overload resolution).
    if (op->resolved_symbol != NULL)
    {
        symbol_t * resolved = op->resolved_symbol;
        if (resolved->value.type_info && resolved->value.type_info->kind == TD_KIND_PROC)
        {
            proc_type = resolved->value.type_info;
            if (resolved->value.value)
            {
                *val = resolved->value.value;
            }
            else
            {
                *val = LLVMGetNamedFunction(ctx->module, resolved->name);
                if (*val == NULL)
                    *val = LLVMAddFunction(ctx->module, resolved->name, proc_type->proc_metadata.func_type);
                resolved->value.value = *val;
            }
        }
    }

    // Priority 2: look up the identifier in the current scope
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
    {
        odin_grammar_node_t * ident = expression_unwrap_to_identifier(node->list.children[0]);
        if (ident)
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
            if (sym)
            {
                if (sym->is_polymorphic && op->resolved_symbol == NULL)
                {
                    ir_gen_error_collection_add(
                        &ctx->errors, NULL, node,
                        "call to polymorphic procedure is not yet supported"
                    );
                    return true;
                }
                if (proc_type == NULL)
                    proc_type = sym->value.type_info;
            }
        }
    }

    // Priority 3: fall back to cur_type (e.g., function pointer calls)
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
    {
        if (*cur_type && (*cur_type)->kind == TD_KIND_PROC)
            proc_type = *cur_type;
    }

    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "called value is not a procedure");
        return true;
    }

    LLVMTypeRef func_type = proc_type->proc_metadata.func_type;

    LLVMValueRef args[128];
    TypeDescriptor const * arg_types[128];
    int arg_count = 0;

    if (op->list.count > 0 && op->list.children[0] != NULL)
    {
        odin_grammar_node_t * arg_expr = op->list.children[0];
        if (arg_expr->type == AST_NODE_ARGUMENT_LIST && arg_expr->list.count > 0)
            arg_expr = arg_expr->list.children[0];
        arg_count = ir_gen_collect_call_args(ctx, arg_expr, args, arg_types, 128);
    }

    // Wrap arguments that match 'any' parameters (non-variadic)
    {
        TypeDescriptor const * any_type = get_basic_type_by_name(ctx->type_registry, "any");
        if (any_type != NULL && any_type->llvm_type != NULL)
        {
            LLVMTypeRef any_llvm = any_type->llvm_type;
            int param_count = proc_type->proc_metadata.param_count;
            for (int pi = 0; pi < param_count && pi < arg_count; pi++)
            {
                TypeDescriptor const * param_type = proc_type->proc_metadata.params[pi];
                if (param_type && param_type->kind == TD_KIND_BASIC
                    && param_type->as.basic.name && strcmp(param_type->as.basic.name, "any") == 0)
                {
                    if (arg_types[pi] && arg_types[pi]->kind == TD_KIND_BASIC
                        && arg_types[pi]->as.basic.name && strcmp(arg_types[pi]->as.basic.name, "any") == 0)
                        continue;
                    if (args[pi] != NULL && LLVMTypeOf(args[pi]) == any_llvm)
                        continue;
                    LLVMValueRef any_alloca = LLVMBuildAlloca(ctx->builder, any_llvm, "any.arg");
                    ir_gen_pack_any(ctx, any_alloca, args[pi], any_llvm, arg_types[pi]);
                    args[pi] = LLVMBuildLoad2(ctx->builder, any_llvm, any_alloca, "any.loaded");
                }
            }
        }
    }

    // Variadic ..any packing: build []any slice from extra args (ODIN convention)
    bool is_any_variadic = false;
    if (proc_type->proc_metadata.is_variadic
        && proc_type->proc_metadata.calling_convention == CALLING_CONV_ODIN
        && proc_type->proc_metadata.param_count >= 1)
    {
        TypeDescriptor const * last_param = proc_type->proc_metadata.params[proc_type->proc_metadata.param_count - 1];
        if (last_param != NULL && last_param->kind == TD_KIND_SLICE)
            is_any_variadic = true;
    }
    if (is_any_variadic
        && arg_count >= proc_type->proc_metadata.param_count - 1)
    {
        int param_count = proc_type->proc_metadata.param_count;
        int fixed_count = param_count - 1;
        int variadic_count = arg_count - fixed_count;
        TypeDescriptor const * any_type = get_basic_type_by_name(ctx->type_registry, "any");
        if (any_type)
        {
            TypeDescriptor const * slice_type = get_or_create_slice_type(ctx->type_registry, any_type);
            LLVMTypeRef any_llvm = any_type->llvm_type;
            LLVMTypeRef slice_llvm = slice_type ? slice_type->llvm_type : NULL;
            if (any_llvm && slice_llvm)
            {
                LLVMValueRef backing = LLVMBuildAlloca(ctx->builder,
                    LLVMArrayType(any_llvm, variadic_count), "variadic.backing");
                for (int vi = 0; vi < variadic_count; vi++)
                {
                    LLVMValueRef gep_idx[2] = {
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), vi, false)
                    };
                    LLVMValueRef slot = LLVMBuildInBoundsGEP2(ctx->builder,
                        LLVMArrayType(any_llvm, variadic_count), backing, gep_idx, 2, "var.slot");
                    ir_gen_pack_any(ctx, slot, args[fixed_count + vi], any_llvm, arg_types[fixed_count + vi]);
                }
                LLVMValueRef backing_ptr = LLVMBuildBitCast(ctx->builder, backing,
                    LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), "var.backing.cast");
                LLVMValueRef slice_val = LLVMGetUndef(slice_llvm);
                slice_val = LLVMBuildInsertValue(ctx->builder, slice_val, backing_ptr, 0, "var.ptr");
                slice_val = LLVMBuildInsertValue(ctx->builder, slice_val,
                    LLVMConstInt(LLVMInt64TypeInContext(ctx->context), variadic_count, false), 1, "var.len");
                args[fixed_count] = slice_val;
                arg_count = fixed_count + 1;
            }
        }
    }
    if (proc_type->proc_metadata.calling_convention == CALLING_CONV_C)
    {
        int param_count = proc_type->proc_metadata.param_count;
        for (int pi = 0; pi < arg_count && pi < param_count; pi++)
        {
            TypeDescriptor const * param_type = proc_type->proc_metadata.params[pi];
            if (param_type == NULL || param_type->kind != TD_KIND_POINTER)
                continue;
            LLVMTypeRef arg_llvm_type = LLVMTypeOf(args[pi]);
            if (arg_llvm_type == NULL || LLVMGetTypeKind(arg_llvm_type) != LLVMStructTypeKind)
                continue;
            args[pi] = LLVMBuildExtractValue(ctx->builder, args[pi], 0, "str.ptr");
        }
    }

    // Phase 4: Prepend implicit context parameter for ODIN calling convention
    if (proc_type->proc_metadata.calling_convention == CALLING_CONV_ODIN)
    {
        if (arg_count < 128)
        {
            for (int j = arg_count; j > 0; j--)
                args[j] = args[j - 1];
            symbol_t * ctx_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
            if (ctx_sym)
            {
                args[0] = ctx_sym->value.value;
            }
            else
            {
                TypeDescriptor const * ctx_type = type_descriptor_get_context_type(ctx->type_registry);
                if (ctx_type)
                {
                    LLVMValueRef ctx_alloca
                        = LLVMBuildAlloca(ctx->builder, ctx_type->llvm_type, "context.temp");
                    LLVMBuildStore(ctx->builder, LLVMConstNull(ctx_type->llvm_type), ctx_alloca);
                    args[0] = ctx_alloca;
                }
                else
                {
                    args[0] = LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
                }
            }
            arg_count++;
        }
    }

    // Phase 5: Coerce integer/float argument values to the declared
    // parameter types, and load aggregate values from alloca pointers.
    {
        int param_count = proc_type->proc_metadata.param_count;
        int context_offset = (proc_type->proc_metadata.calling_convention == CALLING_CONV_ODIN) ? 1 : 0;
        int effective_param_count = param_count;
        if (is_any_variadic)
            effective_param_count = param_count;

        for (int pi = 0; pi < effective_param_count && pi + context_offset < arg_count; pi++)
        {
            int arg_idx = pi + context_offset;
            LLVMValueRef arg_val = args[arg_idx];
            if (arg_val == NULL)
                continue;

            TypeDescriptor const * param_type = proc_type->proc_metadata.params[pi];
            if (param_type == NULL || param_type->llvm_type == NULL)
                continue;

            LLVMTypeRef arg_llvm_type = LLVMTypeOf(arg_val);
            if (arg_llvm_type == param_type->llvm_type)
                continue;

            // Load aggregate values from alloca pointers when param expects value
            if (LLVMGetTypeKind(arg_llvm_type) == LLVMPointerTypeKind
                && (param_type->kind == TD_KIND_ARRAY || param_type->kind == TD_KIND_STRUCT))
            {
                args[arg_idx] = LLVMBuildLoad2(ctx->builder, param_type->llvm_type, arg_val, "arg.load");
                continue;
            }

            // Integer/float coercion (basic types only)
            if (param_type->kind != TD_KIND_BASIC)
                continue;

            LLVMTypeKind arg_kind = LLVMGetTypeKind(arg_llvm_type);
            LLVMTypeKind param_kind = LLVMGetTypeKind(param_type->llvm_type);
            bool arg_is_int = (arg_kind == LLVMIntegerTypeKind);
            bool param_is_int = (param_kind == LLVMIntegerTypeKind);
            bool arg_is_float = (arg_kind == LLVMHalfTypeKind || arg_kind == LLVMFloatTypeKind || arg_kind == LLVMDoubleTypeKind);
            bool param_is_float = (param_kind == LLVMHalfTypeKind || param_kind == LLVMFloatTypeKind || param_kind == LLVMDoubleTypeKind);
            if (!((arg_is_int && param_is_int) || (arg_is_float && param_is_float)))
                continue;

            bool src_is_unsigned = false;
            if (arg_types && arg_types[pi] && arg_types[pi]->kind == TD_KIND_BASIC)
                src_is_unsigned = arg_types[pi]->as.basic.is_unsigned;

            args[arg_idx] = coerce_value_to_type(
                ctx, arg_val, param_type->llvm_type, src_is_unsigned, "arg.coerce"
            );
        }
    }

    *val = LLVMBuildCall2(
        ctx->builder,
        func_type,
        *val,
        args,
        (unsigned)arg_count,
        proc_type->proc_metadata.is_void_return ? "" : "calltmp"
    );

    if (proc_type->proc_metadata.return_type)
        *cur_type = proc_type->proc_metadata.return_type;
    return false;
}

static void
ir_gen_postfix_subscript(IrGenContext * ctx, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    odin_grammar_node_t * index_expr = NULL;
    for (size_t ci = 0; ci < op->list.count; ci++)
    {
        odin_grammar_node_t * child = op->list.children[ci];
        if (child != NULL)
        {
            index_expr = child;
            break;
        }
    }
    if (index_expr == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "missing index expression in subscript");
        return;
    }

    LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
    if (index_val == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, index_expr, "invalid index expression in subscript");
        return;
    }

    if (*cur_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot subscript unknown type");
        return;
    }

    if ((*cur_type)->kind == TD_KIND_ARRAY)
    {
        if (ctx->bounds_checking_enabled)
        {
            LLVMValueRef len_val = LLVMConstInt(
                LLVMInt64TypeInContext(ctx->context), (long long)(*cur_type)->as.array.count, false
            );
            index_val = ir_gen_emit_bounds_check(ctx, index_val, len_val, op);
        }
        LLVMTypeRef arr_type = (*cur_type)->llvm_type;
        LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false), index_val};
        LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, *val, indices, 2, "subs");
        *val = elem_ptr;
    }
    else if ((*cur_type)->kind == TD_KIND_SLICE || (*cur_type)->kind == TD_KIND_DYNAMIC_ARRAY)
    {
        if (ctx->bounds_checking_enabled)
        {
            LLVMValueRef len_indices[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
            LLVMValueRef len_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, (*cur_type)->llvm_type, *val, len_indices, 2, "slice.re.len.ptr"
            );
            LLVMValueRef len_val = LLVMBuildLoad2(
                ctx->builder, LLVMInt64TypeInContext(ctx->context), len_ptr, "slice.re.len"
            );
            index_val = ir_gen_emit_bounds_check(ctx, index_val, len_val, op);
        }
        LLVMValueRef data_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef data_field
            = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, *val, data_indices, 2, "slice.data.ptr");
        LLVMValueRef data = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType((*cur_type)->element_type->llvm_type, 0), data_field, "slice.data"
        );

        *val = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->element_type->llvm_type, data, &index_val, 1, "slice.subs"
        );
    }
    else if ((*cur_type)->kind == TD_KIND_BASIC && (*cur_type)->as.basic.name != NULL
             && strcmp((*cur_type)->as.basic.name, "string") == 0)
    {
        LLVMValueRef struct_val;
        LLVMTypeRef val_type = LLVMTypeOf(*val);
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
            struct_val = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *val, "str.load");
        else
            struct_val = *val;

        if (ctx->bounds_checking_enabled)
        {
            LLVMValueRef len_val = LLVMBuildExtractValue(
                ctx->builder, struct_val, 1, "str.re.len"
            );
            index_val = ir_gen_emit_bounds_check(ctx, index_val, len_val, op);
        }
        LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, struct_val, 0, "str.data");
        LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, LLVMInt8TypeInContext(ctx->context), data, &index_val, 1, "str.subs"
        );
        *val = LLVMBuildLoad2(ctx->builder, LLVMInt8TypeInContext(ctx->context), elem_ptr, "str.char");
        *cur_type = get_basic_type_by_name(ctx->type_registry, "u8");
    }
    else if ((*cur_type)->kind == TD_KIND_VECTOR)
    {
        LLVMValueRef vec_val = *val;
        if (LLVMGetTypeKind(LLVMTypeOf(vec_val)) == LLVMPointerTypeKind)
            vec_val = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, vec_val, "vec.load");
        *val = LLVMBuildExtractElement(ctx->builder, vec_val, index_val, "vec.elem");
        *cur_type = (*cur_type)->element_type;
    }
    else if ((*cur_type)->kind == TD_KIND_MULTI_POINTER)
    {
        LLVMValueRef data = NULL;
        LLVMTypeRef val_type = LLVMTypeOf(*val);
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
            data = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *val, "mp.data");
        else
            data = *val;
        LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->element_type->llvm_type, data, &index_val, 1, "mp.subs"
        );
        *val = elem_ptr;
    }
    else if ((*cur_type)->kind == TD_KIND_MAP)
    {
        *val = ir_gen_map_subscript(ctx, *val, *cur_type, index_val, NULL, false, "mr.");
        if (*val == NULL)
            return;
    }
    else
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot subscript type: not an array, slice, dynamic array, multi-pointer, or map");
        return;
    }

    if ((*cur_type)->kind == TD_KIND_MAP && op->resolved_type)
        *cur_type = op->resolved_type;
    else if ((*cur_type)->element_type)
        *cur_type = (*cur_type)->element_type;
    else if (op->resolved_type)
        *cur_type = op->resolved_type;
}

static void
ir_gen_postfix_member(IrGenContext * ctx, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    odin_grammar_node_t * field_name_node = NULL;
    for (size_t ci = 0; ci < op->list.count; ci++)
    {
        odin_grammar_node_t * child = op->list.children[ci];
        if (child != NULL && child->type == AST_NODE_IDENTIFIER)
        {
            field_name_node = child;
            break;
        }
    }
    if (field_name_node == NULL || field_name_node->text == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "missing field name in member access");
        return;
    }

    // Package-qualified access: pkg.member (resolved by semantic analyser)
    if (*cur_type == NULL && op->resolved_symbol != NULL)
    {
        *val = op->resolved_symbol->value.value;
        *cur_type = op->resolved_symbol->value.type_info;
        return;
    }

    // Maybe(T).value — access payload field
    if (*cur_type && (*cur_type)->kind == TD_KIND_MAYBE && field_name_node->text
        && strcmp(field_name_node->text, "value") == 0)
    {
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *val,
            (LLVMValueRef[]) {
                LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)
            }, 2, "maybe.val.gep"
        );
        *val = LLVMBuildLoad2(ctx->builder, (*cur_type)->as.maybe.inner_type->llvm_type, payload_ptr, "maybe.val.load");
        *cur_type = (*cur_type)->as.maybe.inner_type;
        return;
    }

    // Vector swizzle: v.xyzw -> extractelement/shufflevector
    if (*cur_type && (*cur_type)->kind == TD_KIND_VECTOR && field_name_node->text)
    {
        char const * swiz = field_name_node->text;
        int swiz_len = (int)strlen(swiz);
        LLVMValueRef vec_val = *val;
        if (LLVMGetTypeKind(LLVMTypeOf(vec_val)) == LLVMPointerTypeKind)
            vec_val = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, vec_val, "vec.load");

        if (swiz_len == 1)
        {
            int idx = 0;
            char c = swiz[0];
            if (c == 'x' || c == 'r') idx = 0;
            else if (c == 'y' || c == 'g') idx = 1;
            else if (c == 'z' || c == 'b') idx = 2;
            else if (c == 'w' || c == 'a') idx = 3;
            LLVMValueRef index = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), idx, false);
            *val = LLVMBuildExtractElement(ctx->builder, vec_val, index, swiz);
            *cur_type = (*cur_type)->element_type;
        }
        else
        {
            LLVMValueRef undef_val = LLVMGetUndef((*cur_type)->llvm_type);
            int indices[4];
            for (int si = 0; si < swiz_len; si++)
            {
                char c = swiz[si];
                if (c == 'x' || c == 'r') indices[si] = 0;
                else if (c == 'y' || c == 'g') indices[si] = 1;
                else if (c == 'z' || c == 'b') indices[si] = 2;
                else if (c == 'w' || c == 'a') indices[si] = 3;
            }
            LLVMValueRef mask_vals[4];
            for (int si = 0; si < swiz_len; si++)
                mask_vals[si] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), indices[si], false);
            LLVMValueRef mask_vec = LLVMConstVector(mask_vals, swiz_len);
            *val = LLVMBuildShuffleVector(ctx->builder, vec_val, undef_val, mask_vec, swiz);
            *cur_type = get_or_create_vector_type(ctx->type_registry, (*cur_type)->element_type, swiz_len);
        }
        return;
    }

    // Pointer auto-dereference: p.field -> access member on pointee
    if (ir_gen_is_dereferenceable(*cur_type))
    {
        TypeDescriptor const * pointee = (*cur_type)->pointee;
        if (pointee)
            *cur_type = pointee;
    }

    // Aggregate member access: string, slice, dynamic_array (.len, .data, .cap)
    {
        LLVMTypeRef struct_type = NULL;
        int field_idx = -1;
        TypeDescriptor const * resolved_type = NULL;
        char const * error_name = NULL;
        if (ir_gen_resolve_aggregate_field(ctx, *cur_type, field_name_node->text, &struct_type, &field_idx, &resolved_type, &error_name))
        {
            LLVMValueRef ptr_val = *val;
            if (LLVMGetTypeKind(LLVMTypeOf(ptr_val)) != LLVMPointerTypeKind)
            {
                LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, struct_type, "agg.ptr");
                LLVMBuildStore(ctx->builder, ptr_val, tmp);
                ptr_val = tmp;
            }
            LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            LLVMValueRef field_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_idx, false)};
            *val = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, ptr_val, field_indices, 2, field_name_node->text);
            *cur_type = resolved_type;
            return;
        }
        if (error_name)
        {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "%s has no field named", error_name);
            ir_gen_error_collection_add(&ctx->errors, NULL, op, err_buf);
            return;
        }
    }

    // Array member access: .len -> int (compile-time constant)
    if (*cur_type && (*cur_type)->kind == TD_KIND_ARRAY)
    {
        if (strcmp(field_name_node->text, "len") == 0)
        {
            *val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context),
                (unsigned long long)(*cur_type)->as.array.count, false);
            *cur_type = get_basic_type_by_name(ctx->type_registry, "int");
        }
        else
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "array has no field named");
        }
        return;
    }

    if (*cur_type == NULL
        || ((*cur_type)->kind != TD_KIND_STRUCT && (*cur_type)->kind != TD_KIND_SOA && (*cur_type)->kind != TD_KIND_UNION
        ))
    {
        if (*cur_type && (*cur_type)->kind == TD_KIND_BIT_FIELD)
        {
            char const * field_name = field_name_node->text;
            bit_field_field_info const * bf = type_descriptor_find_bit_field_field(*cur_type, field_name);
            if (bf == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, NULL, op, "bit_field has no field named");
                return;
            }

            LLVMValueRef backing = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *val, "bf.backing");
            LLVMValueRef shifted = backing;
            if (bf->offset_bits > 0)
            {
                LLVMValueRef off = LLVMConstInt((*cur_type)->llvm_type, (unsigned)bf->offset_bits, false);
                shifted = LLVMBuildLShr(ctx->builder, backing, off, "bf.shifted");
            }
            uint64_t mask_val = (bf->width_bits >= 64) ? ~0ULL : ((1ULL << bf->width_bits) - 1);
            LLVMValueRef mask = LLVMConstInt((*cur_type)->llvm_type, mask_val, false);
            LLVMValueRef extracted = LLVMBuildAnd(ctx->builder, shifted, mask, "bf.extracted");
            *val = LLVMBuildIntCast(ctx->builder, extracted, bf->type->llvm_type, "bf.val");
            *cur_type = bf->type;
        }
        else if (*cur_type)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "type has no member");
        }
        return;
    }

    if ((*cur_type)->kind == TD_KIND_UNION)
    {
        int field_idx = type_descriptor_find_union_field_index(*cur_type, field_name_node->text);
        if (field_idx < 0)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "union has no field named");
            return;
        }
        struct_field_t const * field = type_descriptor_get_union_field(*cur_type, field_idx);
        if (field == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "union field lookup failed");
            return;
        }

        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef payload_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *val, payload_indices, 2, "union.payload.gep"
        );
        *val = LLVMBuildPointerCast(
            ctx->builder, payload_ptr, LLVMPointerType(field->type_desc->llvm_type, 0), field_name_node->text
        );
        *cur_type = field->type_desc;
        return;
    }

    field_access_path_t path;
    if (!type_descriptor_find_struct_field_path(*cur_type, field_name_node->text, &path))
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "type has no field");
        return;
    }

    int n_indices = path.count + 1;
    LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    for (int pi = 0; pi < path.count; pi++)
    {
        indices[pi + 1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
    }

    LLVMValueRef field_ptr = LLVMBuildInBoundsGEP2(
        ctx->builder, (*cur_type)->llvm_type, *val, indices, (unsigned)n_indices, field_name_node->text
    );
    *val = field_ptr;

    TypeDescriptor const * tmp_type = *cur_type;
    for (int pi = 0; pi < path.count; pi++)
    {
        struct_field_t const * f = type_descriptor_get_struct_field(tmp_type, path.indices[pi]);
        if (f == NULL)
            break;
        if (pi == path.count - 1)
            *cur_type = f->type_desc;
        else
            tmp_type = f->type_desc;
    }
}

static void
ir_gen_postfix_deref(IrGenContext * ctx, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    if (!ir_gen_is_dereferenceable(*cur_type))
    {
        if (*cur_type)
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot dereference non-pointer type");
        return;
    }
    TypeDescriptor const * pointee_type = (*cur_type)->pointee;
    if (pointee_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot dereference pointer to void");
        return;
    }
    *val = LLVMBuildLoad2(ctx->builder, pointee_type->llvm_type, *val, "deref");
    *cur_type = pointee_type;
}

static void
ir_gen_postfix_assertion(IrGenContext * ctx, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    TypeDescriptor const * target_type = op->resolved_type;
    if (target_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "type assertion has no resolved target type");
        return;
    }

    // Type assertion x.(T) for 'any': extract data pointer, bitcast, load
    if (*cur_type && (*cur_type)->kind == TD_KIND_BASIC && (*cur_type)->as.basic.name
        && strcmp((*cur_type)->as.basic.name, "any") == 0)
    {
        LLVMTypeRef any_val_type = LLVMTypeOf(*val);
        if (LLVMGetTypeKind(any_val_type) == LLVMPointerTypeKind)
            *val = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *val, "assert.any.load");
        LLVMValueRef tmp_alloca = LLVMBuildAlloca(ctx->builder, (*cur_type)->llvm_type, "assert.tmp");
        LLVMBuildStore(ctx->builder, *val, tmp_alloca);
        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
        LLVMValueRef gep_id[2] = {idx0, idx1};
        LLVMValueRef id_field = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, tmp_alloca, gep_id, 2, "assert.typeid.ptr"
        );
        LLVMValueRef stored_type_id
            = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), id_field, "assert.typeid");
        int64_t expected_tid = (int64_t)target_type->type_id;
        LLVMValueRef expected_type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), expected_tid, false);
        LLVMValueRef type_match
            = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_type_id, expected_type_id, "assert.match");
        LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
        LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.match");
        LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.fail");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.cont");
        LLVMBuildCondBr(ctx->builder, type_match, match_bb, fail_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
        LLVMValueRef gep_data[2] = {idx0, idx0};
        LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, tmp_alloca, gep_data, 2, "assert.data.ptr"
        );
        LLVMValueRef data_ptr = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_field, "assert.data"
        );
        if (target_type->kind == TD_KIND_BASIC && !target_type->as.basic.is_float
            && target_type->as.basic.width > 0 && target_type->as.basic.width <= 64)
        {
            LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, data_ptr,
                LLVMPointerType(target_type->llvm_type, 0), "assert.typed");
            *val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "assert.val");
        }
        else if (target_type->kind == TD_KIND_POINTER)
        {
            *val = LLVMBuildBitCast(ctx->builder, data_ptr, target_type->llvm_type, "assert.val");
        }
        else
        {
            LLVMValueRef typed_ptr = LLVMBuildBitCast(
                ctx->builder, data_ptr, LLVMPointerType(target_type->llvm_type, 0), "assert.typed"
            );
            *val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "assert.val");
        }
        *cur_type = target_type;
        LLVMBuildBr(ctx->builder, cont_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, fail_bb);
        {
            LLVMValueRef trap_func = LLVMGetNamedFunction(ctx->module, "llvm.trap");
            LLVMTypeRef trap_ftype;
            if (trap_func == NULL)
            {
                trap_ftype = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, false);
                trap_func = LLVMAddFunction(ctx->module, "llvm.trap", trap_ftype);
            }
            else
            {
                trap_ftype = LLVMGlobalGetValueType(trap_func);
            }
            LLVMBuildCall2(ctx->builder, trap_ftype, trap_func, NULL, 0, "");
        }
        LLVMBuildUnreachable(ctx->builder);
        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    }
    else if (*cur_type && (*cur_type)->kind == TD_KIND_MAYBE)
    {
        LLVMValueRef ptr = *val;
        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
        LLVMValueRef tag_indices[2] = {idx0, idx0};
        LLVMValueRef tag_ptr = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, ptr, tag_indices, 2, "massert.tag.gep");
        LLVMValueRef stored_tag = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), tag_ptr, "massert.tag");
        LLVMValueRef expected_tag = LLVMConstNull(LLVMInt64TypeInContext(ctx->context));
        LLVMValueRef tag_match = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_tag, expected_tag, "massert.match");
        LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
        LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.match");
        LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.fail");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.cont");
        LLVMBuildCondBr(ctx->builder, tag_match, match_bb, fail_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
        LLVMValueRef payload_indices[2] = {idx0, idx1};
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, ptr, payload_indices, 2, "massert.payload.gep");
        LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, payload_ptr, LLVMPointerType(target_type->llvm_type, 0), "massert.typed");
        *val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "massert.val");
        *cur_type = target_type;
        LLVMBuildBr(ctx->builder, cont_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, fail_bb);
        {
            LLVMValueRef trap_func = LLVMGetNamedFunction(ctx->module, "llvm.trap");
            LLVMTypeRef trap_ftype;
            if (trap_func == NULL)
            {
                trap_ftype = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, false);
                trap_func = LLVMAddFunction(ctx->module, "llvm.trap", trap_ftype);
            }
            else
            {
                trap_ftype = LLVMGlobalGetValueType(trap_func);
            }
            LLVMBuildCall2(ctx->builder, trap_ftype, trap_func, NULL, 0, "");
        }
        LLVMBuildUnreachable(ctx->builder);
        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    }
    else if (*cur_type && (*cur_type)->kind == TD_KIND_UNION)
    {
        int field_idx = (int)(intptr_t)op->resolved_symbol;
        if (field_idx < 0)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "union type assertion field not found");
            return;
        }

        LLVMValueRef ptr = *val;
        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
        LLVMValueRef tag_indices[2] = {idx0, idx0};
        LLVMValueRef tag_ptr
            = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, ptr, tag_indices, 2, "uassert.tag.gep");
        LLVMValueRef stored_tag
            = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), tag_ptr, "uassert.tag");
        LLVMValueRef expected_tag
            = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)field_idx, false);
        LLVMValueRef tag_match
            = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_tag, expected_tag, "uassert.match");
        LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
        LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.match");
        LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.fail");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.cont");
        LLVMBuildCondBr(ctx->builder, tag_match, match_bb, fail_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
        LLVMValueRef payload_indices[2] = {idx0, idx1};
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, ptr, payload_indices, 2, "uassert.payload.gep"
        );
        LLVMValueRef typed_ptr = LLVMBuildPointerCast(
            ctx->builder, payload_ptr, LLVMPointerType(target_type->llvm_type, 0), "uassert.typed"
        );
        *val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "uassert.val");
        *cur_type = target_type;
        LLVMBuildBr(ctx->builder, cont_bb);
        LLVMPositionBuilderAtEnd(ctx->builder, fail_bb);
        {
            LLVMValueRef trap_func = LLVMGetNamedFunction(ctx->module, "llvm.trap");
            LLVMTypeRef trap_ftype;
            if (trap_func == NULL)
            {
                trap_ftype = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, false);
                trap_func = LLVMAddFunction(ctx->module, "llvm.trap", trap_ftype);
            }
            else
            {
                trap_ftype = LLVMGlobalGetValueType(trap_func);
            }
            LLVMBuildCall2(ctx->builder, trap_ftype, trap_func, NULL, 0, "");
        }
        LLVMBuildUnreachable(ctx->builder);
        LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    }
}

typedef struct
{
    int has_low;
    int has_high;
    odin_grammar_node_t * low_expr;
    odin_grammar_node_t * high_expr;
} slice_bounds_info;

static slice_bounds_info
slice_get_bounds_info(odin_grammar_node_t * op)
{
    slice_bounds_info info = {0};
    if (op == NULL || op->text == NULL)
        return info;

    char const * text = op->text;
    size_t len = strlen(text);

    // Parse "[i..j]", "[i..]", "[..j]", "[..]"
    // Skip leading '['
    size_t pos = 0;
    while (pos < len && text[pos] == '[')
        pos++;

    // Check for low bound
    if (pos < len && text[pos] != '.')
    {
        info.has_low = 1;
        // Find the end of the low expression (stop before '..')
        size_t start = pos;
        while (pos + 1 < len && !(text[pos] == '.' && text[pos + 1] == '.'))
            pos++;
        // We don't extract the node here — just indicate presence
    }

    // Skip '..'
    while (pos + 1 < len && text[pos] == '.' && text[pos + 1] == '.')
        pos += 2;

    // Check for high bound
    if (pos < len && text[pos] != ']' && text[pos] != '<')
    {
        info.has_high = 1;
    }

    // Find the actual AST children — they are ordered as [low, high]
    if (op->list.count >= 1 && op->list.children[0] != NULL)
    {
        if (info.has_low)
            info.low_expr = op->list.children[0];
        if (info.has_high && op->list.count >= 2 && op->list.children[1] != NULL)
            info.high_expr = op->list.children[1];
    }

    return info;
}

static void
ir_gen_postfix_slice(IrGenContext * ctx, odin_grammar_node_t * op, LLVMValueRef * val, TypeDescriptor const ** cur_type)
{
    if (*cur_type == NULL || ((*cur_type)->kind != TD_KIND_SLICE && (*cur_type)->kind != TD_KIND_ARRAY))
    {
        if (*cur_type)
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot slice: type is not a slice or array");
        return;
    }

    TypeDescriptor const * slice_type = *cur_type;
    TypeDescriptor const * elem_type = slice_type->element_type;
    if (elem_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "slice/array has no element type");
        return;
    }

    LLVMValueRef base_ptr = *val;
    if (LLVMGetTypeKind(LLVMTypeOf(base_ptr)) != LLVMPointerTypeKind)
    {
        LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, (*cur_type)->llvm_type, "slice.base.tmp");
        LLVMBuildStore(ctx->builder, base_ptr, tmp);
        base_ptr = tmp;
    }

    LLVMValueRef data, len;

    if ((*cur_type)->kind == TD_KIND_SLICE)
    {
        LLVMValueRef field0_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef data_gep = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, base_ptr, field0_indices, 2, "slice.data.gep"
        );
        data = LLVMBuildLoad2(ctx->builder, LLVMPointerType(elem_type->llvm_type, 0), data_gep, "slice.data");
        LLVMValueRef field1_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMValueRef len_gep
            = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, base_ptr, field1_indices, 2, "slice.len.gep");
        len = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), len_gep, "slice.len");
    }
    else
    {
        slice_type = get_or_create_slice_type(ctx->type_registry, elem_type);
        LLVMValueRef zero_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        data = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, base_ptr, zero_indices, 2, "arr.ptr");
        len = LLVMConstInt(
            LLVMInt64TypeInContext(ctx->context), (unsigned long long)(*cur_type)->as.array.count, false
        );
    }

    slice_bounds_info bounds = slice_get_bounds_info(op);

    LLVMValueRef new_data = data;
    LLVMValueRef new_len = len;

    if (bounds.has_low && bounds.low_expr != NULL)
    {
        LLVMValueRef low_val = ir_gen_node(ctx, bounds.low_expr);
        if (low_val)
        {
            new_data
                = LLVMBuildInBoundsGEP2(ctx->builder, elem_type->llvm_type, data, &low_val, 1, "slice.newdata");
            new_len = LLVMBuildSub(ctx->builder, len, low_val, "slice.newlen");
        }
    }

    if (bounds.has_high && bounds.high_expr != NULL)
    {
        LLVMValueRef high_val = ir_gen_node(ctx, bounds.high_expr);
        if (high_val)
        {
            if (bounds.has_low)
            {
                LLVMValueRef low_val = ir_gen_node(ctx, bounds.low_expr);
                if (low_val)
                    new_len = LLVMBuildSub(ctx->builder, high_val, low_val, "slice.newlen");
            }
            else
            {
                new_len = high_val;
            }
        }
    }

    LLVMValueRef slice_ptr = LLVMBuildAlloca(ctx->builder, slice_type->llvm_type, "slice.tmp");
    LLVMValueRef result_data_indices[]
        = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
           LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
    LLVMValueRef data_gep = LLVMBuildInBoundsGEP2(
        ctx->builder, slice_type->llvm_type, slice_ptr, result_data_indices, 2, "slice.data.gep"
    );
    LLVMBuildStore(ctx->builder, new_data, data_gep);
    LLVMValueRef result_len_indices[]
        = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
           LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
    LLVMValueRef len_gep = LLVMBuildInBoundsGEP2(
        ctx->builder, slice_type->llvm_type, slice_ptr, result_len_indices, 2, "slice.len.gep"
    );
    LLVMBuildStore(ctx->builder, new_len, len_gep);
    *val = LLVMBuildLoad2(ctx->builder, slice_type->llvm_type, slice_ptr, "slice.res");
    *cur_type = slice_type;
}

LLVMValueRef
ir_gen_postfix_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    LLVMValueRef val = ir_gen_node(ctx, node->list.children[0]);

    if (node->list.count < 2)
        return val;
    odin_grammar_node_t * postfix_ops = node->list.children[1];
    // No actual postfix operations — return the base value as-is
    if (postfix_ops == NULL || postfix_ops->type != AST_NODE_POSTFIX_OPS || postfix_ops->list.count == 0)
        return val;

    odin_grammar_node_t * pe_child = node->list.children[0];
    TypeDescriptor const * cur_type = NULL;

    // Prefer scope symbol type (correct for specialization-specific types over shared AST resolved_type)
    if (pe_child != NULL)
    {
        odin_grammar_node_t * ident = expression_unwrap_to_identifier(pe_child);
        if (ident && ident->text)
        {
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
            if (sym && sym->value.type_info)
                cur_type = sym->value.type_info;
        }
    }
    if (cur_type == NULL && pe_child != NULL)
        cur_type = pe_child->resolved_type;

    for (size_t i = 0; i < postfix_ops->list.count; i++)
    {
        odin_grammar_node_t * op = postfix_ops->list.children[i];
        if (op == NULL)
            continue;

        switch (op->type)
        {
        case AST_NODE_POSTFIX_CALL:
            if (ir_gen_postfix_call(ctx, node, op, &val, &cur_type))
                return val;
            break;
        case AST_NODE_POSTFIX_SUBSCRIPT:
            ir_gen_postfix_subscript(ctx, op, &val, &cur_type);
            break;
        case AST_NODE_POSTFIX_MEMBER:
            ir_gen_postfix_member(ctx, op, &val, &cur_type);
            break;
        case AST_NODE_POSTFIX_DEREF:
            ir_gen_postfix_deref(ctx, op, &val, &cur_type);
            break;
        case AST_NODE_POSTFIX_ASSERTION:
            ir_gen_postfix_assertion(ctx, op, &val, &cur_type);
            break;
        case AST_NODE_POSTFIX_SLICE:
        case AST_NODE_POSTFIX_SLICE_LT:
            ir_gen_postfix_slice(ctx, op, &val, &cur_type);
            break;
        default:
            break;
        }
    }

    // If the final value is a pointer and the result type is non-composite, load
    // it. Proc types are excluded because the pointer IS the value (function
    // pointer). Basic pointer-valued types (cstring, rawptr) are excluded
    // because the value IS a pointer, not a pointer-to-value.
    if (val != NULL && cur_type != NULL)
    {
        LLVMTypeRef val_llvm_type = LLVMTypeOf(val);
        if (LLVMGetTypeKind(val_llvm_type) == LLVMPointerTypeKind)
        {
            bool is_ptr_valued_basic = (cur_type->kind == TD_KIND_BASIC
                && LLVMGetTypeKind(cur_type->llvm_type) == LLVMPointerTypeKind);
            if (cur_type->kind != TD_KIND_STRUCT && cur_type->kind != TD_KIND_SOA && cur_type->kind != TD_KIND_ARRAY
                && cur_type->kind != TD_KIND_SLICE && cur_type->kind != TD_KIND_PROC
                && cur_type->kind != TD_KIND_DYNAMIC_ARRAY && cur_type->kind != TD_KIND_MAP
                && cur_type->kind != TD_KIND_BIT_FIELD && cur_type->kind != TD_KIND_BIT_SET
                && cur_type->kind != TD_KIND_UNION && cur_type->kind != TD_KIND_MULTI_POINTER
                && !is_ptr_valued_basic)
            {
                val = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "loadtmp");
            }
        }
    }

    return val;
}

