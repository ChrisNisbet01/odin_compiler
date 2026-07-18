#include "ir_gen_assign.h"
#include "semantic_analyser.h"
#include "type_descriptors.h"
#include "llvm_ir_generator.h"
#include "odin_grammar_ast.h"
#include "generator_lists.h"
#include "ast_utils.h"
#include "scope.h"
#include "ir_gen_statement.h"

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// --- Forward declarations ---
static LLVMValueRef ir_gen_assign_store(
    IrGenContext * ctx,
    odin_grammar_node_t * lhs_node,
    LLVMValueRef rhs_val,
    OperatorKind op_kind,
    odin_grammar_node_t * rhs_expr_node
);

// --- Assignment codegen ---

// Recursively unwrap expression wrapper nodes to find the identifier child.
// Wrapper nodes simply delegate to their first child.
bool
is_expression_wrapper_type(odin_grammar_node_type_t type)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (type)
    {
    case AST_NODE_EXPRESSION:
    case AST_NODE_ASSIGN_EXPRESSION:
    case AST_NODE_OR_ELSE:
    case AST_NODE_TERNARY_EXPRESSION:
    case AST_NODE_RANGE_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_UNARY_EXPRESSION:
    case AST_NODE_POSTFIX_EXPRESSION:
    case AST_NODE_PRIMARY_EXPRESSION:
        return true;
    default:
        return false;
    }

#pragma GCC diagnostic pop
}

odin_grammar_node_t *
expression_unwrap_to_identifier(odin_grammar_node_t * node)
{
    while (node != NULL && is_expression_wrapper_type(node->type))
    {
        if (node->list.count > 0)
            node = node->list.children[0];
        else
            return NULL;
    }
    if (node != NULL && node->type == AST_NODE_IDENTIFIER)
        return node;
    return NULL;
}

// slice_bounds_info and slice_get_bounds_info moved to ir_gen_postfix.c

static LLVMBasicBlockRef
ir_gen_map_append_block(IrGenContext * ctx, char const * prefix, char const * suffix)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s", prefix, suffix);
    return LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), buf);
}

bool
ir_gen_is_dereferenceable(TypeDescriptor const * td)
{
    return td != NULL && (td->kind == TD_KIND_POINTER || td->kind == TD_KIND_MULTI_POINTER);
}

LLVMValueRef
ir_gen_map_subscript(IrGenContext * ctx, LLVMValueRef map_val,
                     TypeDescriptor const * map_type,
                     LLVMValueRef index_val,
                     TypeDescriptor const ** out_val_type,
                     bool is_lvalue, char const * prefix)
{
    LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(ctx->context);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, false);
    LLVMValueRef one64 = LLVMConstInt(i64t, 1, false);
    LLVMValueRef zero32 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);

    TypeDescriptor const * key_td = map_type->as.map.key_type;
    TypeDescriptor const * val_td = map_type->as.map.value_type;

    if (out_val_type)
        *out_val_type = val_td;

    LLVMValueRef data_ptr = NULL;
    if (is_lvalue || LLVMGetTypeKind(LLVMTypeOf(map_val)) == LLVMPointerTypeKind)
    {
        LLVMValueRef didx[] = {zero32, zero32};
        LLVMValueRef data_gep = LLVMBuildInBoundsGEP2(
            ctx->builder, map_type->llvm_type, map_val, didx, 2, "");
        data_ptr = LLVMBuildLoad2(ctx->builder, LLVMPointerType(i8t, 0), data_gep, "");
    }
    else
    {
        data_ptr = LLVMBuildExtractValue(ctx->builder, map_val, 0, "");
    }
    if (data_ptr == NULL)
        return NULL;

    LLVMValueRef cap_off = LLVMConstInt(i64t, 8, false);
    LLVMValueRef cap_ptr = LLVMBuildPointerCast(ctx->builder,
        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &cap_off, 1, ""),
        LLVMPointerType(i64t, 0), "");
    LLVMValueRef cap_val = LLVMBuildLoad2(ctx->builder, i64t, cap_ptr, "");

    LLVMValueRef ks_off = LLVMConstInt(i64t, 16, false);
    LLVMValueRef ks_ptr = LLVMBuildPointerCast(ctx->builder,
        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &ks_off, 1, ""),
        LLVMPointerType(i64t, 0), "");
    LLVMValueRef key_sz = LLVMBuildLoad2(ctx->builder, i64t, ks_ptr, "");

    LLVMValueRef vs_off = LLVMConstInt(i64t, 24, false);
    LLVMValueRef vs_ptr = LLVMBuildPointerCast(ctx->builder,
        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &vs_off, 1, ""),
        LLVMPointerType(i64t, 0), "");
    LLVMValueRef val_sz = LLVMBuildLoad2(ctx->builder, i64t, vs_ptr, "");

    LLVMValueRef hdr32 = LLVMConstInt(i64t, 32, false);
    LLVMValueRef entries_base = LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &hdr32, 1, "");

    LLVMValueRef stride = LLVMBuildAdd(ctx->builder, one64,
        LLVMBuildAdd(ctx->builder, key_sz, val_sz, ""), "");
    LLVMValueRef ks_plus_one = LLVMBuildAdd(ctx->builder, key_sz, one64, "");

    LLVMValueRef key_to_compare = LLVMBuildIntCast(ctx->builder, index_val, key_td->llvm_type, "");

    LLVMValueRef res_alloca = NULL;
    LLVMValueRef fe_alloca = NULL;
    LLVMValueRef cnt_ptr = NULL;
    if (is_lvalue)
    {
        LLVMTypeRef target_ptr_type = LLVMPointerType(val_td->llvm_type, 0);
        res_alloca = LLVMBuildAlloca(ctx->builder, target_ptr_type, "");
        LLVMBuildStore(ctx->builder, LLVMConstNull(target_ptr_type), res_alloca);

        fe_alloca = LLVMBuildAlloca(ctx->builder, i64t, "");
        LLVMBuildStore(ctx->builder, cap_val, fe_alloca);

        LLVMValueRef cnt_off = LLVMConstInt(i64t, 0, false);
        cnt_ptr = LLVMBuildPointerCast(ctx->builder,
            LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &cnt_off, 1, ""),
            LLVMPointerType(i64t, 0), "");
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef loop_bb = ir_gen_map_append_block(ctx, prefix, "loop");
    LLVMBasicBlockRef body_bb = ir_gen_map_append_block(ctx, prefix, "body");
    LLVMBasicBlockRef kchk_bb = ir_gen_map_append_block(ctx, prefix, "kchk");
    LLVMBasicBlockRef found_bb = ir_gen_map_append_block(ctx, prefix, "found");
    LLVMBasicBlockRef next_bb = ir_gen_map_append_block(ctx, prefix, "next");

    LLVMBasicBlockRef empty_bb = NULL;
    LLVMBasicBlockRef after_bb = NULL;
    LLVMBasicBlockRef claim_bb = NULL;
    LLVMBasicBlockRef notfound_bb = NULL;

    if (is_lvalue)
    {
        empty_bb = ir_gen_map_append_block(ctx, prefix, "empty");
        after_bb = ir_gen_map_append_block(ctx, prefix, "after");
        claim_bb = ir_gen_map_append_block(ctx, prefix, "claim");
    }
    else
    {
        notfound_bb = ir_gen_map_append_block(ctx, prefix, "notfound");
    }
    LLVMBasicBlockRef merge_bb = ir_gen_map_append_block(ctx, prefix, "merge");

    LLVMBuildBr(ctx->builder, loop_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, loop_bb);
    LLVMValueRef i_phi = LLVMBuildPhi(ctx->builder, i64t, "");
    LLVMValueRef loop_cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, i_phi, cap_val, "");
    LLVMBuildCondBr(ctx->builder, loop_cmp, body_bb, is_lvalue ? after_bb : notfound_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef ioff = LLVMBuildMul(ctx->builder, i_phi, stride, "");
    LLVMValueRef entry_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entries_base, &ioff, 1, "");
    LLVMValueRef occupied = LLVMBuildLoad2(ctx->builder, i8t, entry_ptr, "");
    LLVMValueRef occ_cmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, occupied, LLVMConstNull(i8t), "");
    LLVMBuildCondBr(ctx->builder, occ_cmp, kchk_bb, is_lvalue ? empty_bb : next_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, kchk_bb);
    LLVMValueRef key_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &one64, 1, "");
    LLVMValueRef kp_typed = LLVMBuildPointerCast(ctx->builder, key_ptr, LLVMPointerType(key_td->llvm_type, 0), "");
    LLVMValueRef loaded_key = LLVMBuildLoad2(ctx->builder, key_td->llvm_type, kp_typed, "");
    LLVMValueRef key_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, key_to_compare, loaded_key, "");
    LLVMBuildCondBr(ctx->builder, key_eq, found_bb, next_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, found_bb);
    LLVMValueRef val_ptr2 = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &ks_plus_one, 1, "");
    LLVMValueRef vp_typed = LLVMBuildPointerCast(ctx->builder, val_ptr2, LLVMPointerType(val_td->llvm_type, 0), "");
    LLVMValueRef loaded_val = NULL;
    if (is_lvalue)
    {
        LLVMBuildStore(ctx->builder, vp_typed, res_alloca);
        LLVMBuildBr(ctx->builder, merge_bb);
    }
    else
    {
        loaded_val = LLVMBuildLoad2(ctx->builder, val_td->llvm_type, vp_typed, "");
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
    LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, i_phi, one64, "");
    LLVMBuildBr(ctx->builder, loop_bb);

    LLVMValueRef i_incoming[] = {zero64, next_i};
    LLVMBasicBlockRef i_blocks[] = {saved_bb, next_bb};
    LLVMAddIncoming(i_phi, i_incoming, i_blocks, 2);

    if (is_lvalue)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, empty_bb);
        LLVMValueRef cur_fe = LLVMBuildLoad2(ctx->builder, i64t, fe_alloca, "");
        LLVMValueRef fe_is_default = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cur_fe, cap_val, "");
        LLVMValueRef new_fe = LLVMBuildSelect(ctx->builder, fe_is_default, i_phi, cur_fe, "");
        LLVMBuildStore(ctx->builder, new_fe, fe_alloca);
        LLVMBuildBr(ctx->builder, next_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, after_bb);
        LLVMValueRef fo_res = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType(val_td->llvm_type, 0), res_alloca, "");
        LLVMValueRef fo_isnull = LLVMBuildICmp(ctx->builder, LLVMIntEQ, fo_res,
            LLVMConstNull(LLVMPointerType(val_td->llvm_type, 0)), "");
        LLVMValueRef fe_val = LLVMBuildLoad2(ctx->builder, i64t, fe_alloca, "");
        LLVMValueRef has_empty = LLVMBuildICmp(ctx->builder, LLVMIntULT, fe_val, cap_val, "");
        LLVMValueRef need_claim = LLVMBuildAnd(ctx->builder, fo_isnull, has_empty, "");
        LLVMBuildCondBr(ctx->builder, need_claim, claim_bb, merge_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, claim_bb);
        LLVMValueRef c_ioff = LLVMBuildMul(ctx->builder, fe_val, stride, "");
        LLVMValueRef c_entry = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entries_base, &c_ioff, 1, "");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8t, 1, false), c_entry);
        LLVMValueRef c_kp = LLVMBuildInBoundsGEP2(ctx->builder, i8t, c_entry, &one64, 1, "");
        LLVMValueRef c_kpt = LLVMBuildPointerCast(ctx->builder, c_kp, LLVMPointerType(key_td->llvm_type, 0), "");
        LLVMBuildStore(ctx->builder, key_to_compare, c_kpt);
        LLVMValueRef old_cnt = LLVMBuildLoad2(ctx->builder, i64t, cnt_ptr, "");
        LLVMBuildStore(ctx->builder, LLVMBuildAdd(ctx->builder, old_cnt, one64, ""), cnt_ptr);
        LLVMValueRef c_vp = LLVMBuildInBoundsGEP2(ctx->builder, i8t, c_entry, &ks_plus_one, 1, "");
        LLVMValueRef c_vpt = LLVMBuildPointerCast(ctx->builder, c_vp, LLVMPointerType(val_td->llvm_type, 0), "");
        LLVMBuildStore(ctx->builder, c_vpt, res_alloca);
        LLVMBuildBr(ctx->builder, merge_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef result_ptr = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType(val_td->llvm_type, 0), res_alloca, "");
        if (result_ptr == NULL)
            return NULL;
        LLVMValueRef tmp_alloca = LLVMBuildAlloca(ctx->builder, val_td->llvm_type, "");
        LLVMValueRef ptr_null = LLVMBuildICmp(ctx->builder, LLVMIntEQ, result_ptr,
            LLVMConstNull(LLVMPointerType(val_td->llvm_type, 0)), "");
        return LLVMBuildSelect(ctx->builder, ptr_null, tmp_alloca, result_ptr, "");
    }
    else
    {
        LLVMPositionBuilderAtEnd(ctx->builder, notfound_bb);
        LLVMValueRef zero_val = LLVMConstNull(val_td->llvm_type);
        LLVMBuildBr(ctx->builder, merge_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef result_phi = LLVMBuildPhi(ctx->builder, val_td->llvm_type, "");
        LLVMValueRef phi_vals[] = {loaded_val, zero_val};
        LLVMBasicBlockRef phi_blocks[] = {found_bb, notfound_bb};
        LLVMAddIncoming(result_phi, phi_vals, phi_blocks, 2);
        return result_phi;
    }
}

// Phase 3.5 helpers — extracted cases from ir_gen_lvalue

static bool
ir_gen_lvalue_postfix_subscript(IrGenContext * ctx, LLVMValueRef * ptr,
                                TypeDescriptor const ** cur_type, odin_grammar_node_t * op)
{
    odin_grammar_node_t * index_expr = NULL;
    for (size_t ci = 0; ci < op->list.count; ci++)
    {
        if (op->list.children[ci] != NULL)
        {
            index_expr = op->list.children[ci];
            break;
        }
    }
    if (index_expr == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "subscript index expression is NULL");
        return false;
    }

    LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
    if (index_val == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "failed to evaluate subscript index");
        return false;
    }

    if (*cur_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "subscript target has unknown type");
        return false;
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
        *ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, *ptr, indices, 2, "subs");

        if ((*cur_type)->element_type)
            *cur_type = (*cur_type)->element_type;
    }
    else if ((*cur_type)->kind == TD_KIND_SLICE || (*cur_type)->kind == TD_KIND_DYNAMIC_ARRAY)
    {
        if (ctx->bounds_checking_enabled)
        {
            LLVMValueRef len_indices[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
            LLVMValueRef len_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, (*cur_type)->llvm_type, *ptr, len_indices, 2, "slice.len.ptr"
            );
            LLVMValueRef len_val = LLVMBuildLoad2(
                ctx->builder, LLVMInt64TypeInContext(ctx->context), len_ptr, "slice.len"
            );
            index_val = ir_gen_emit_bounds_check(ctx, index_val, len_val, op);
        }
        LLVMValueRef data_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *ptr, data_indices, 2, "slice.data.ptr"
        );
        LLVMValueRef data = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType((*cur_type)->element_type->llvm_type, 0), data_field, "slice.data"
        );

        *ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->element_type->llvm_type, data, &index_val, 1, "slice.subs"
        );

        if ((*cur_type)->element_type)
            *cur_type = (*cur_type)->element_type;
    }
    else if ((*cur_type)->kind == TD_KIND_BASIC && (*cur_type)->as.basic.name != NULL
             && strcmp((*cur_type)->as.basic.name, "string") == 0)
    {
        if (ctx->bounds_checking_enabled)
        {
            LLVMValueRef len_indices[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
            LLVMValueRef len_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, (*cur_type)->llvm_type, *ptr, len_indices, 2, "str.len.ptr"
            );
            LLVMValueRef len_val = LLVMBuildLoad2(
                ctx->builder, LLVMInt64TypeInContext(ctx->context), len_ptr, "str.len"
            );
            index_val = ir_gen_emit_bounds_check(ctx, index_val, len_val, op);
        }
        LLVMValueRef data_indices[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *ptr, data_indices, 2, "str.data.ptr"
        );
        LLVMValueRef data = LLVMBuildLoad2(
            ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_field, "str.data"
        );
        *ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, LLVMInt8TypeInContext(ctx->context), data, &index_val, 1, "str.subs"
        );
        *cur_type = get_basic_type_by_name(ctx->type_registry, "u8");
    }
    else if ((*cur_type)->kind == TD_KIND_VECTOR)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op,
            "vector element assignment not supported");
        return false;
    }
    else if ((*cur_type)->kind == TD_KIND_MULTI_POINTER)
    {
        LLVMValueRef data_ptr = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *ptr, "mp.data");
        *ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->element_type->llvm_type, data_ptr, &index_val, 1, "mp.subs"
        );
        if ((*cur_type)->element_type)
            *cur_type = (*cur_type)->element_type;
    }
    else if ((*cur_type)->kind == TD_KIND_MAP)
    {
        *ptr = ir_gen_map_subscript(ctx, *ptr, *cur_type, index_val, cur_type, true, "m.");
        if (*ptr == NULL)
            return false;
    }
    else
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "map subscript: failed to extract data pointer");
        return false;
    }
    return true;
}

static bool
ir_gen_lvalue_postfix_member(IrGenContext * ctx, LLVMValueRef * ptr,
                             TypeDescriptor const ** cur_type, odin_grammar_node_t * op)
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
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "member access: missing field name");
        return false;
    }

    if (*cur_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "member access: current type is unknown");
        return false;
    }

    // Pointer auto-dereference: p.field -> load p, then access member on pointee
    if (ir_gen_is_dereferenceable(*cur_type))
    {
        TypeDescriptor const * pointee = (*cur_type)->pointee;
        if (pointee == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "member access through pointer with unknown pointee");
            return false;
        }
        *ptr = LLVMBuildLoad2(ctx->builder, (*cur_type)->llvm_type, *ptr, "auto.deref");
        *cur_type = pointee;
    }

    if ((*cur_type)->kind == TD_KIND_UNION)
    {
        int field_idx = type_descriptor_find_union_field_index(*cur_type, field_name_node->text);
        if (field_idx < 0)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "union has no field named");
            return false;
        }
        struct_field_t const * field = type_descriptor_get_union_field(*cur_type, field_idx);
        if (field == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, op, "union field lookup returned NULL");
            return false;
        }

        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef tag_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef tag_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *ptr, tag_indices, 2, "union.tag.gep"
        );
        LLVMValueRef tag_val
            = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)field_idx, false);
        LLVMBuildStore(ctx->builder, tag_val, tag_ptr);

        LLVMValueRef payload_indices[2]
            = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, (*cur_type)->llvm_type, *ptr, payload_indices, 2, "union.payload.gep"
        );
        *ptr = LLVMBuildPointerCast(
            ctx->builder,
            payload_ptr,
            LLVMPointerType(field->type_desc->llvm_type, 0),
            field_name_node->text
        );
        *cur_type = field->type_desc;
        return true;
    }

    if ((*cur_type)->kind == TD_KIND_MAYBE)
    {
        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef payload_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(ctx->builder, (*cur_type)->llvm_type, *ptr, payload_indices, 2, "maybe.val.gep");
        *ptr = LLVMBuildBitCast(ctx->builder, payload_ptr, LLVMPointerType((*cur_type)->as.maybe.inner_type->llvm_type, 0), field_name_node->text);
        *cur_type = (*cur_type)->as.maybe.inner_type;
        return true;
    }

    // Aggregate member access: string, slice, dynamic_array (.len, .data, .cap)
    {
        LLVMTypeRef struct_type = NULL;
        int field_idx = -1;
        TypeDescriptor const * resolved_type = NULL;
        char const * error_name = NULL;
        if (ir_gen_resolve_aggregate_field(ctx, *cur_type, field_name_node->text, &struct_type, &field_idx, &resolved_type, &error_name))
        {
            LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            LLVMValueRef field_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_idx, false)};
            *ptr = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, *ptr, field_indices, 2, field_name_node->text);
            *cur_type = resolved_type;
            return true;
        }
        if (error_name)
        {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "%s has no field named", error_name);
            ir_gen_error_collection_add(&ctx->errors, NULL, op, err_buf);
            return false;
        }
    }

    // Array .len -> compile-time constant
    if ((*cur_type)->kind == TD_KIND_ARRAY)
    {
        if (strcmp(field_name_node->text, "len") == 0)
        {
            *cur_type = get_basic_type_by_name(ctx->type_registry, "int");
            LLVMValueRef const_val = LLVMConstInt((*cur_type)->llvm_type,
                (unsigned long long)(*cur_type)->as.array.count, false);
            LLVMValueRef alloca_tmp = LLVMBuildAlloca(ctx->builder, (*cur_type)->llvm_type, "arr.len");
            LLVMBuildStore(ctx->builder, const_val, alloca_tmp);
            *ptr = alloca_tmp;
            return true;
        }
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "array has no field named");
        return false;
    }

    if ((*cur_type)->kind != TD_KIND_STRUCT && (*cur_type)->kind != TD_KIND_SOA)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "member access: type is not a struct");
        return false;
    }

    field_access_path_t path;
    if (!type_descriptor_find_struct_field_path(*cur_type, field_name_node->text, &path))
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, op, "struct has no field named");
        return false;
    }

    int n_indices = path.count + 1;
    LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    for (int pi = 0; pi < path.count; pi++)
    {
        indices[pi + 1]
            = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
    }
    *ptr = LLVMBuildInBoundsGEP2(
        ctx->builder, (*cur_type)->llvm_type, *ptr, indices, (unsigned)n_indices, field_name_node->text
    );

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
    return true;
}

// Evaluate a node as an lvalue (return a pointer to the storage location).
LLVMValueRef
ir_gen_lvalue(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (node->type)
    {
    case AST_NODE_IDENTIFIER:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
        if (sym && sym->value.is_lvalue)
            return sym->value.value;
        // _ (blank identifier) — silently OK, used for discards
        if (node->text && strcmp(node->text, "_") == 0)
            return NULL;
        if (sym == NULL)
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "undeclared identifier in lvalue context");
        else
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "identifier is not an lvalue");
        return NULL;
    }

    case AST_NODE_CONTEXT_EXPR:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
        if (sym && sym->value.is_lvalue)
            return sym->value.value;
        if (node->text && strcmp(node->text, "_") == 0)
            return NULL;
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "'context' is not available in this scope");
        return NULL;
    }

    case AST_NODE_POSTFIX_EXPRESSION:
    {
        if (node->list.count < 2)
            return ir_gen_lvalue(ctx, node->list.children[0]);
        odin_grammar_node_t * base = node->list.children[0];
        odin_grammar_node_t * postfix_ops = node->list.children[1];
        if (postfix_ops == NULL)
            return ir_gen_lvalue(ctx, base);

        LLVMValueRef ptr = ir_gen_lvalue(ctx, base);
        if (ptr == NULL)
            return NULL;

        TypeDescriptor const * cur_type = base ? base->resolved_type : NULL;
        if (cur_type == NULL && base != NULL)
        {
            odin_grammar_node_t * ident = expression_unwrap_to_identifier(base);
            if (ident && ident->text)
            {
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
                if (sym && sym->value.type_info)
                    cur_type = sym->value.type_info;
            }
        }

        for (size_t i = 0; i < postfix_ops->list.count; i++)
        {
            odin_grammar_node_t * op = postfix_ops->list.children[i];
            if (op == NULL)
                continue;

            switch (op->type)
            {
            case AST_NODE_POSTFIX_SUBSCRIPT:
                if (!ir_gen_lvalue_postfix_subscript(ctx, &ptr, &cur_type, op))
                    return NULL;
                break;

            case AST_NODE_POSTFIX_MEMBER:
                if (!ir_gen_lvalue_postfix_member(ctx, &ptr, &cur_type, op))
                    return NULL;
                break;

            case AST_NODE_POSTFIX_DEREF:
            {
                if (!ir_gen_is_dereferenceable(cur_type))
                {
                    if (cur_type)
                        ir_gen_error_collection_add(&ctx->errors, NULL, op, "cannot dereference non-pointer type in lvalue context");
                    return NULL;
                }
                ptr = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, ptr, "deref");
                if (cur_type->pointee)
                    cur_type = cur_type->pointee;
                break;
            }

            default:
                break;
            }
        }
        return ptr;
    }

    default:
    {
        if (is_expression_wrapper_type(node->type) && node->list.count > 0)
            return ir_gen_lvalue(ctx, node->list.children[0]);
        return NULL;
    }
    }

#pragma GCC diagnostic pop
}

static OperatorKind
compound_assign_to_binary_op(OperatorKind compound_kind)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (compound_kind)
    {
    case OP_ADD_ASSIGN:
        return OP_ADD;
    case OP_SUB_ASSIGN:
        return OP_SUB;
    case OP_MUL_ASSIGN:
        return OP_MUL;
    case OP_DIV_ASSIGN:
        return OP_DIV;
    case OP_MOD_ASSIGN:
        return OP_MOD;
    case OP_AND_ASSIGN:
        return OP_BIT_AND;
    case OP_OR_ASSIGN:
        return OP_BIT_OR;
    case OP_XOR_ASSIGN:
        return OP_BIT_XOR;
    case OP_SHL_ASSIGN:
        return OP_SHL;
    case OP_SHR_ASSIGN:
        return OP_SHR;
    default:
        return OP_INVALID;
    }

#pragma GCC diagnostic pop
}

static LLVMValueRef
ir_gen_binary_op_by_kind(IrGenContext * ctx, LLVMValueRef lhs, LLVMValueRef rhs, OperatorKind kind)
{
    if (lhs == NULL || rhs == NULL)
        return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float = (type_kind == LLVMHalfTypeKind || type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (kind)
    {
    case OP_ADD:
        return is_float ? LLVMBuildFAdd(ctx->builder, lhs, rhs, "addtmp")
                        : LLVMBuildAdd(ctx->builder, lhs, rhs, "addtmp");
    case OP_SUB:
        return is_float ? LLVMBuildFSub(ctx->builder, lhs, rhs, "subtmp")
                        : LLVMBuildSub(ctx->builder, lhs, rhs, "subtmp");
    case OP_MUL:
        return is_float ? LLVMBuildFMul(ctx->builder, lhs, rhs, "multmp")
                        : LLVMBuildMul(ctx->builder, lhs, rhs, "multmp");
    case OP_DIV:
        return is_float ? LLVMBuildFDiv(ctx->builder, lhs, rhs, "divtmp")
                        : LLVMBuildSDiv(ctx->builder, lhs, rhs, "divtmp");
    case OP_MOD:
        return is_float ? LLVMBuildFRem(ctx->builder, lhs, rhs, "modtmp")
                        : LLVMBuildSRem(ctx->builder, lhs, rhs, "modtmp");
    case OP_SHL:
        return LLVMBuildShl(ctx->builder, lhs, rhs, "shltmp");
    case OP_SHR:
        return LLVMBuildAShr(ctx->builder, lhs, rhs, "shrtmp");
    case OP_BIT_AND:
        return LLVMBuildAnd(ctx->builder, lhs, rhs, "andtmp");
    case OP_BIT_OR:
        return LLVMBuildOr(ctx->builder, lhs, rhs, "ortmp");
    case OP_BIT_XOR:
        return LLVMBuildXor(ctx->builder, lhs, rhs, "xortmp");
    default:
        return NULL;
    }

#pragma GCC diagnostic pop
}

LLVMValueRef
ir_gen_lvalue_ptr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    LLVMValueRef lhs_ptr = ir_gen_lvalue(ctx, node);
    if (lhs_ptr == NULL)
    {
        odin_grammar_node_t * lhs_id = expression_unwrap_to_identifier(node);
        if (lhs_id == NULL)
            return NULL;
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), lhs_id->text);
        if (sym && sym->value.is_lvalue)
            lhs_ptr = sym->value.value;
    }
    return lhs_ptr;
}

LLVMValueRef
ir_gen_or_else_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // OrElseExpr = TernaryExpression (KwOrElse TernaryExpression)?
    // KwOrElse lexeme is consumed but not added as a child node.
    // When or_else is present: node has 2 children [Ternary, Ternary]
    // When absent: node has 1 child [Ternary]
    if (node->list.count < 2)
    {
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
    if (lhs == NULL)
        return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);

    // Check if LHS is Maybe(T) — check tag instead of zero
    TypeDescriptor const * lhs_td = node->list.children[0]->resolved_type;
    if (lhs_td && lhs_td->kind == TD_KIND_MAYBE)
    {
        // Load Maybe struct value (lhs may be pointer from composite type identifier)
        LLVMTypeRef maybe_llvm = lhs_td->llvm_type;
        LLVMValueRef maybe_val = lhs;
        if (LLVMGetTypeKind(LLVMTypeOf(lhs)) == LLVMPointerTypeKind)
            maybe_val = LLVMBuildLoad2(ctx->builder, maybe_llvm, lhs, "maybe.load");
        LLVMTypeRef inner_llvm = lhs_td->as.maybe.inner_type->llvm_type;
        LLVMValueRef tag = LLVMBuildExtractValue(ctx->builder, maybe_val, 0, "maybe.tag");
        LLVMValueRef payload = LLVMBuildExtractValue(ctx->builder, maybe_val, 1, "maybe.val");
        LLVMValueRef is_none = LLVMBuildICmp(ctx->builder, LLVMIntNE, tag, LLVMConstNull(LLVMInt64TypeInContext(ctx->context)), "maybe.isnone");

        LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.rhs");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.merge");

        LLVMBuildCondBr(ctx->builder, is_none, rhs_bb, merge_bb);

        // RHS block
        LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
        LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[1]);
        if (rhs == NULL)
            rhs = LLVMConstNull(inner_llvm);
        LLVMBuildBr(ctx->builder, merge_bb);

        // Re-capture RHS end block (RHS may have split rhs_bb via bounds checks)
        LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(ctx->builder);

        // Merge with phi
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(ctx->builder, inner_llvm, "or.phi");
        LLVMValueRef phi_vals[2] = {payload, rhs};
        LLVMBasicBlockRef phi_blocks[2] = {entry_bb, rhs_end_bb};
        LLVMAddIncoming(phi, phi_vals, phi_blocks, 2);
        return phi;
    }

    LLVMValueRef is_zero;
    LLVMTypeKind tk = LLVMGetTypeKind(lhs_type);
    if (tk == LLVMIntegerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, LLVMConstNull(lhs_type), "orelse_isnil");
    else if (tk == LLVMPointerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, LLVMConstNull(lhs_type), "orelse_isnil");
    else
        return lhs;

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.rhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.merge");

    LLVMBuildCondBr(ctx->builder, is_zero, rhs_bb, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[1]);
    if (rhs == NULL)
        rhs = LLVMConstNull(lhs_type);
    LLVMBuildBr(ctx->builder, merge_bb);

    // Re-capture RHS end block (RHS may have split rhs_bb via bounds checks)
    LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, lhs_type, "or.phi");
    LLVMValueRef incoming_vals[] = {lhs, rhs};
    LLVMBasicBlockRef incoming_blocks[] = {entry_bb, rhs_end_bb};
    LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);

    return phi;
}

LLVMValueRef
ir_gen_or_return_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // OrReturnExpr = OrElseExpr KwOrReturn
    // Only present when or_return keyword is used; always do short-circuit.
    if (node->list.count < 1)
        return NULL;

    LLVMValueRef val = ir_gen_node(ctx, node->list.children[0]);
    if (val == NULL)
        return NULL;

    LLVMTypeRef val_type = LLVMTypeOf(val);

    LLVMValueRef is_zero;
    LLVMTypeKind tk = LLVMGetTypeKind(val_type);
    if (tk == LLVMIntegerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, val, LLVMConstNull(val_type), "orret_isnil");
    else if (tk == LLVMPointerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, val, LLVMConstNull(val_type), "orret_isnil");
    else
        return val;

    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.ret");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.cont");

    LLVMBuildCondBr(ctx->builder, is_zero, ret_bb, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ret_bb);
    ir_gen_emit_all_defers(ctx);
    ir_gen_implicit_return(ctx);

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    return val;
}

LLVMValueRef
ir_gen_ternary_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // TernaryExpression = RangeExpression (? Expression : Expression)?
    // Without ?: 1 child [cond]; with ?: 3 children [cond, true, false]
    if (node->list.count < 3)
    {
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    LLVMValueRef cond = ir_gen_node(ctx, node->list.children[0]);
    if (cond == NULL)
        return NULL;

    LLVMTypeRef cond_type = LLVMTypeOf(cond);
    LLVMValueRef is_truthy;
    LLVMTypeKind tk = LLVMGetTypeKind(cond_type);
    if (tk == LLVMIntegerTypeKind)
    {
        LLVMValueRef zero = LLVMConstNull(cond_type);
        is_truthy = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, zero, "tcond");
    }
    else if (tk == LLVMPointerTypeKind)
    {
        is_truthy = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond, LLVMConstNull(cond_type), "tcond");
    }
    else
    {
        is_truthy = cond;
    }

    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "tern.then");
    LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "tern.else");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "tern.merge");

    LLVMBuildCondBr(ctx->builder, is_truthy, then_bb, else_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
    LLVMValueRef true_val = ir_gen_node(ctx, node->list.children[1]);
    if (true_val == NULL)
        true_val = LLVMConstNull(cond_type);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMBasicBlockRef then_end_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
    LLVMValueRef false_val = ir_gen_node(ctx, node->list.children[2]);
    if (false_val == NULL)
        false_val = LLVMConstNull(cond_type);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMBasicBlockRef else_end_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMTypeRef result_type = LLVMTypeOf(true_val);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, result_type, "tern.phi");
    LLVMValueRef incoming_vals[] = {true_val, false_val};
    LLVMBasicBlockRef incoming_blocks[] = {then_end_bb, else_end_bb};
    LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);

    return phi;
}

// Handle assignment expressions: AssignExpression = OrReturnExpr (AssignOp
// OrReturnExpr)?

// Pack a value into an 'any' struct {i8* data, i64 type_id}.
// lhs_ptr must be a pointer to an any struct alloca.
void
ir_gen_pack_any(
    IrGenContext * ctx,
    LLVMValueRef lhs_ptr,
    LLVMValueRef rhs_val,
    LLVMTypeRef any_struct_type,
    TypeDescriptor const * source_type
)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMValueRef zero_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);

    // If the value is already an `any` struct, copy directly
    LLVMTypeRef val_type = LLVMTypeOf(rhs_val);
    if (val_type == any_struct_type)
    {
        LLVMBuildStore(ctx->builder, rhs_val, lhs_ptr);
        return;
    }

    LLVMValueRef data_ptr;
    LLVMTypeKind val_kind = LLVMGetTypeKind(val_type);
    if (val_kind == LLVMPointerTypeKind)
    {
        data_ptr = LLVMBuildBitCast(ctx->builder, rhs_val, i8ptr, "anydata");
    }
    else
    {
        LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, val_type, "anytmp");
        LLVMBuildStore(ctx->builder, rhs_val, tmp);
        data_ptr = LLVMBuildBitCast(ctx->builder, tmp, i8ptr, "anydata");
    }
    int64_t tid = (source_type != NULL) ? (int64_t)source_type->type_id : 0;
    LLVMValueRef type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), tid, false);
    LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    LLVMValueRef gep0[2] = {zero_idx, idx1};
    LLVMValueRef data_field = LLVMBuildInBoundsGEP2(ctx->builder, any_struct_type, lhs_ptr, gep0, 2, "any.data");
    LLVMBuildStore(ctx->builder, data_ptr, data_field);
    LLVMValueRef idx2 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
    LLVMValueRef gep1[2] = {zero_idx, idx2};
    LLVMValueRef id_field = LLVMBuildInBoundsGEP2(ctx->builder, any_struct_type, lhs_ptr, gep1, 2, "any.typeid");
    LLVMBuildStore(ctx->builder, type_id, id_field);
}

static bool
ir_gen_bit_field_write(IrGenContext * ctx, odin_grammar_node_t * lhs_expr, LLVMValueRef rhs_val, OperatorKind op_kind)
{
    if (lhs_expr == NULL)
        return false;
    while (lhs_expr->type != AST_NODE_POSTFIX_EXPRESSION && is_expression_wrapper_type(lhs_expr->type)
           && lhs_expr->list.count >= 1)
        lhs_expr = lhs_expr->list.children[0];
    if (lhs_expr->type != AST_NODE_POSTFIX_EXPRESSION || lhs_expr->list.count < 2)
        return false;
    odin_grammar_node_t * base = lhs_expr->list.children[0];
    odin_grammar_node_t * ops = lhs_expr->list.children[1];
    if (base == NULL || base->resolved_type == NULL || base->resolved_type->kind != TD_KIND_BIT_FIELD)
        return false;
    if (ops == NULL || ops->list.count == 0)
        return false;
    odin_grammar_node_t * last_op = ops->list.children[ops->list.count - 1];
    if (last_op == NULL || last_op->type != AST_NODE_POSTFIX_MEMBER)
        return false;
    char const * field_name = NULL;
    for (size_t ci = 0; ci < last_op->list.count; ci++)
    {
        odin_grammar_node_t * child = last_op->list.children[ci];
        if (child != NULL && child->type == AST_NODE_IDENTIFIER)
        {
            field_name = child->text;
            break;
        }
    }
    if (field_name == NULL)
        return false;
    bit_field_field_info const * bf = type_descriptor_find_bit_field_field(base->resolved_type, field_name);
    if (bf == NULL)
        return false;
    LLVMValueRef backing_ptr = ir_gen_lvalue_ptr(ctx, base);
    if (backing_ptr == NULL)
        return true;
    LLVMTypeRef backing_type = base->resolved_type->llvm_type;
    uint64_t mask_val = (bf->width_bits >= 64) ? ~0ULL : ((1ULL << bf->width_bits) - 1);
    LLVMValueRef mask = LLVMConstInt(backing_type, mask_val, false);
    if (op_kind == OP_ASSIGN)
    {
        LLVMValueRef old_backing = LLVMBuildLoad2(ctx->builder, backing_type, backing_ptr, "bf.old");
        LLVMValueRef shifted_mask = mask;
        if (bf->offset_bits > 0)
        {
            LLVMValueRef off = LLVMConstInt(backing_type, (unsigned)bf->offset_bits, false);
            shifted_mask = LLVMBuildShl(ctx->builder, mask, off, "bf.smask");
        }
        LLVMValueRef cleared = LLVMBuildAnd(
            ctx->builder, old_backing, LLVMBuildNot(ctx->builder, shifted_mask, "bf.nmask"), "bf.cleared"
        );
        LLVMValueRef rhs_cast = LLVMBuildIntCast(ctx->builder, rhs_val, backing_type, "bf.rhscast");
        LLVMValueRef shifted_rhs = rhs_cast;
        if (bf->offset_bits > 0)
        {
            LLVMValueRef off = LLVMConstInt(backing_type, (unsigned)bf->offset_bits, false);
            shifted_rhs = LLVMBuildShl(ctx->builder, rhs_cast, off, "bf.shrhs");
        }
        LLVMBuildStore(ctx->builder, LLVMBuildOr(ctx->builder, cleared, shifted_rhs, "bf.new"), backing_ptr);
    }
    else
    {
        OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
        LLVMValueRef old_backing = LLVMBuildLoad2(ctx->builder, backing_type, backing_ptr, "bf.old");
        LLVMValueRef shifted_old = old_backing;
        if (bf->offset_bits > 0)
        {
            LLVMValueRef off = LLVMConstInt(backing_type, (unsigned)bf->offset_bits, false);
            shifted_old = LLVMBuildLShr(ctx->builder, old_backing, off, "bf.shiftold");
        }
        LLVMValueRef old_field_val = LLVMBuildAnd(ctx->builder, shifted_old, mask, "bf.oldfield");
        LLVMValueRef old_field_cast = LLVMBuildIntCast(ctx->builder, old_field_val, bf->type->llvm_type, "bf.oldcast");
        LLVMValueRef result = ir_gen_binary_op_by_kind(ctx, old_field_cast, rhs_val, bin_op);
        LLVMValueRef result_cast = LLVMBuildIntCast(ctx->builder, result, backing_type, "bf.result");
        LLVMValueRef shifted_mask = mask;
        if (bf->offset_bits > 0)
        {
            LLVMValueRef off = LLVMConstInt(backing_type, (unsigned)bf->offset_bits, false);
            shifted_mask = LLVMBuildShl(ctx->builder, mask, off, "bf.smask");
        }
        LLVMValueRef cleared = LLVMBuildAnd(
            ctx->builder, old_backing, LLVMBuildNot(ctx->builder, shifted_mask, "bf.nmask"), "bf.cleared"
        );
        LLVMValueRef shifted_result = result_cast;
        if (bf->offset_bits > 0)
        {
            LLVMValueRef off = LLVMConstInt(backing_type, (unsigned)bf->offset_bits, false);
            shifted_result = LLVMBuildShl(ctx->builder, result_cast, off, "bf.shres");
        }
        LLVMBuildStore(ctx->builder, LLVMBuildOr(ctx->builder, cleared, shifted_result, "bf.new"), backing_ptr);
    }
    return true;
}

static bool
ir_gen_bit_set_assign_expr(
    IrGenContext * ctx,
    odin_grammar_node_t * lhs_expr,
    odin_grammar_node_t * rhs_node,
    LLVMValueRef rhs_val,
    OperatorKind op_kind
)
{
    if (lhs_expr == NULL || rhs_node == NULL)
        return false;
    odin_grammar_node_t * t = expression_chain_unwrap(lhs_expr);
    TypeDescriptor const * lhs_td = t ? t->resolved_type : NULL;
    if (lhs_td == NULL || lhs_td->kind != TD_KIND_BIT_SET)
        return false;

    LLVMValueRef ptr = ir_gen_lvalue_ptr(ctx, lhs_expr);
    if (ptr == NULL)
        return true;

    if (op_kind == OP_ASSIGN)
    {
        LLVMBuildStore(ctx->builder, rhs_val, ptr);
        return true;
    }

    // Determine if RHS is also a bit_set
    odin_grammar_node_t * rt = expression_chain_unwrap(rhs_node);
    TypeDescriptor const * rhs_td = rt ? rt->resolved_type : NULL;
    bool rhs_is_bit_set = (rhs_td && rhs_td->kind == TD_KIND_BIT_SET);

    LLVMValueRef backing = LLVMBuildLoad2(ctx->builder, lhs_td->llvm_type, ptr, "bs.load");
    OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
    if (bin_op == OP_INVALID)
        return true;

    if (rhs_is_bit_set && bin_op == OP_SUB)
    {
        LLVMValueRef result
            = LLVMBuildAnd(ctx->builder, backing, LLVMBuildNot(ctx->builder, rhs_val, "bs.not"), "bs.diff");
        LLVMBuildStore(ctx->builder, result, ptr);
    }
    else if (!rhs_is_bit_set && (bin_op == OP_ADD || bin_op == OP_BIT_OR))
    {
        LLVMValueRef elem_cast = LLVMBuildIntCast(ctx->builder, rhs_val, lhs_td->llvm_type, "bs.elem");
        LLVMValueRef one = LLVMConstInt(lhs_td->llvm_type, 1, false);
        LLVMValueRef mask = LLVMBuildShl(ctx->builder, one, elem_cast, "bs.mask");
        LLVMValueRef result = LLVMBuildOr(ctx->builder, backing, mask, "bs.incl");
        LLVMBuildStore(ctx->builder, result, ptr);
    }
    else if (!rhs_is_bit_set && bin_op == OP_SUB)
    {
        LLVMValueRef elem_cast = LLVMBuildIntCast(ctx->builder, rhs_val, lhs_td->llvm_type, "bs.elem");
        LLVMValueRef one = LLVMConstInt(lhs_td->llvm_type, 1, false);
        LLVMValueRef mask = LLVMBuildShl(ctx->builder, one, elem_cast, "bs.mask");
        LLVMValueRef result
            = LLVMBuildAnd(ctx->builder, backing, LLVMBuildNot(ctx->builder, mask, "bs.nmask"), "bs.excl");
        LLVMBuildStore(ctx->builder, result, ptr);
    }
    else
    {
        // Generic: both bit_set (|, &, ~) or other cases
        LLVMValueRef result = ir_gen_binary_op_by_kind(ctx, backing, rhs_val, bin_op);
        LLVMBuildStore(ctx->builder, result, ptr);
    }
    return true;
}

static bool
ir_gen_vector_elem_assign(
    IrGenContext * ctx,
    odin_grammar_node_t * lhs_expr,
    LLVMValueRef rhs_val,
    OperatorKind op_kind,
    odin_grammar_node_t * rhs_node
)
{
    if (lhs_expr == NULL)
        return false;

    // Unwrap expression wrapper nodes to find the POSTFIX_EXPRESSION inside
    odin_grammar_node_t * postfix_expr = lhs_expr;
    while (postfix_expr->type != AST_NODE_POSTFIX_EXPRESSION
           && is_expression_wrapper_type(postfix_expr->type)
           && postfix_expr->list.count >= 1 && postfix_expr->list.children[0])
        postfix_expr = postfix_expr->list.children[0];

    if (postfix_expr->type != AST_NODE_POSTFIX_EXPRESSION
        || postfix_expr->list.count < 2)
        return false;

    odin_grammar_node_t * base = postfix_expr->list.children[0];
    odin_grammar_node_t * postfix_ops = postfix_expr->list.children[1];
    if (base == NULL || postfix_ops == NULL)
        return false;

    TypeDescriptor const * base_type = base->resolved_type;
    if (base_type == NULL || base_type->kind != TD_KIND_VECTOR)
        return false;

    odin_grammar_node_t * subscript_op = NULL;
    odin_grammar_node_t * member_op = NULL;
    for (size_t i = 0; i < postfix_ops->list.count; i++)
    {
        odin_grammar_node_t * op = postfix_ops->list.children[i];
        if (op)
        {
            if (op->type == AST_NODE_POSTFIX_SUBSCRIPT)
                subscript_op = op;
            else if (op->type == AST_NODE_POSTFIX_MEMBER)
                member_op = op;
        }
    }

    if (subscript_op)
    {
        odin_grammar_node_t * index_expr = NULL;
        for (size_t ci = 0; ci < subscript_op->list.count; ci++)
        {
            if (subscript_op->list.children[ci] != NULL)
            {
                index_expr = subscript_op->list.children[ci];
                break;
            }
        }
        if (index_expr == NULL)
            return false;

        LLVMValueRef vec_ptr = ir_gen_lvalue(ctx, base);
        if (vec_ptr == NULL)
        {
            odin_grammar_node_t * ident = expression_unwrap_to_identifier(base);
            if (ident == NULL)
                return false;
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
            if (sym == NULL || !sym->value.is_lvalue)
                return false;
            vec_ptr = sym->value.value;
        }
        if (vec_ptr == NULL)
            return false;

        LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
        if (index_val == NULL)
            return true;

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, base_type->llvm_type, vec_ptr, "vec.load");
        LLVMTypeRef elem_type = base_type->element_type ? base_type->element_type->llvm_type : NULL;

        LLVMValueRef store_val = rhs_val;
        if (elem_type)
            store_val = coerce_value_to_type(ctx, rhs_val, elem_type, false, "vec.ins");

        if (op_kind != OP_ASSIGN)
        {
            OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
            if (bin_op == OP_INVALID)
                return true;
            LLVMValueRef elem_val = LLVMBuildExtractElement(ctx->builder, vec_val, index_val, "vec.elem");
            LLVMValueRef result = ir_gen_binary_op_by_kind(ctx, elem_val, store_val, bin_op);
            vec_val = LLVMBuildInsertElement(ctx->builder, vec_val, result, index_val, "vec.ins");
        }
        else
        {
            vec_val = LLVMBuildInsertElement(ctx->builder, vec_val, store_val, index_val, "vec.ins");
        }

        LLVMBuildStore(ctx->builder, vec_val, vec_ptr);
        return true;
    }
    else if (member_op)
    {
        // Swizzle lvalue: v.xy = val
        odin_grammar_node_t * field_name_node = NULL;
        for (size_t ci = 0; ci < member_op->list.count; ci++)
        {
            if (member_op->list.children[ci] && member_op->list.children[ci]->type == AST_NODE_IDENTIFIER)
            {
                field_name_node = member_op->list.children[ci];
                break;
            }
        }
        if (field_name_node == NULL || field_name_node->text == NULL)
            return false;

        char const * swiz = field_name_node->text;
        int swiz_len = (int)strlen(swiz);
        if (swiz_len == 0 || swiz_len > 4)
            return false;

        int swiz_indices[4];
        for (int si = 0; si < swiz_len; si++)
        {
            char c = swiz[si];
            if (c == 'x' || c == 'r') swiz_indices[si] = 0;
            else if (c == 'y' || c == 'g') swiz_indices[si] = 1;
            else if (c == 'z' || c == 'b') swiz_indices[si] = 2;
            else swiz_indices[si] = 3;
        }

        LLVMValueRef vec_ptr = ir_gen_lvalue(ctx, base);
        if (vec_ptr == NULL)
        {
            odin_grammar_node_t * ident = expression_unwrap_to_identifier(base);
            if (ident == NULL)
                return false;
            symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
            if (sym == NULL || !sym->value.is_lvalue)
                return false;
            vec_ptr = sym->value.value;
        }
        if (vec_ptr == NULL)
            return false;

        LLVMValueRef vec_val = LLVMBuildLoad2(ctx->builder, base_type->llvm_type, vec_ptr, "vec.load");
        LLVMTypeRef elem_type = base_type->element_type ? base_type->element_type->llvm_type : NULL;
        int lane_count = base_type->as.vector.lane_count;

        if (swiz_len == 1)
        {
            // Single-component: v.x = val
            LLVMValueRef index_val = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), swiz_indices[0], false);
            LLVMValueRef store_val = rhs_val;
            if (elem_type)
                store_val = coerce_value_to_type(ctx, rhs_val, elem_type, false, "vec.ins");

            if (op_kind != OP_ASSIGN)
            {
                OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
                if (bin_op == OP_INVALID)
                    return true;
                LLVMValueRef elem_val = LLVMBuildExtractElement(ctx->builder, vec_val, index_val, "vec.elem");
                LLVMValueRef result = ir_gen_binary_op_by_kind(ctx, elem_val, store_val, bin_op);
                vec_val = LLVMBuildInsertElement(ctx->builder, vec_val, result, index_val, "vec.ins");
            }
            else
            {
                vec_val = LLVMBuildInsertElement(ctx->builder, vec_val, store_val, index_val, "vec.ins");
            }

            LLVMBuildStore(ctx->builder, vec_val, vec_ptr);
        }
        else
        {
            // Multi-component: v.xy = val
            // Use InsertElement for each lane to avoid ShuffleVector
            // mismatched-length operand restrictions
            LLVMValueRef rhs_vec = rhs_val;
            if (LLVMGetTypeKind(LLVMTypeOf(rhs_vec)) == LLVMPointerTypeKind)
            {
                TypeDescriptor const * swiz_type = member_op->resolved_type;
                if (swiz_type && swiz_type->llvm_type)
                    rhs_vec = LLVMBuildLoad2(ctx->builder, swiz_type->llvm_type, rhs_vec, "rhs.vec.load");
            }

            LLVMValueRef result_vec = vec_val;
            if (op_kind != OP_ASSIGN)
            {
                OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
                if (bin_op == OP_INVALID)
                    return true;

                for (int si = 0; si < swiz_len; si++)
                {
                    int dst_lane = swiz_indices[si];
                    LLVMValueRef dst_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), dst_lane, false);
                    LLVMValueRef src_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), si, false);
                    LLVMValueRef elem_val = LLVMBuildExtractElement(ctx->builder, result_vec, dst_idx, "vec.elem");
                    LLVMValueRef rhs_elem = LLVMBuildExtractElement(ctx->builder, rhs_vec, src_idx, "rhs.elem");
                    LLVMValueRef res_val = ir_gen_binary_op_by_kind(ctx, elem_val, rhs_elem, bin_op);
                    result_vec = LLVMBuildInsertElement(ctx->builder, result_vec, res_val, dst_idx, "vec.ins");
                }
            }
            else
            {
                for (int si = 0; si < swiz_len; si++)
                {
                    int dst_lane = swiz_indices[si];
                    LLVMValueRef dst_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), dst_lane, false);
                    LLVMValueRef src_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), si, false);
                    LLVMValueRef elem_val = LLVMBuildExtractElement(ctx->builder, rhs_vec, src_idx, "rhs.elem");
                    if (elem_type)
                        elem_val = coerce_value_to_type(ctx, elem_val, elem_type, false, "vec.ins");
                    result_vec = LLVMBuildInsertElement(ctx->builder, result_vec, elem_val, dst_idx, "vec.ins");
                }
            }

            LLVMBuildStore(ctx->builder, result_vec, vec_ptr);
        }
        return true;
    }

    return false;
}

LLVMValueRef
ir_gen_assign_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 3)
    {
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    odin_grammar_node_t * op_node = node_find_op(node);
    AstOpMetadata * op_md = (op_node) ? (AstOpMetadata *)op_node->metadata : NULL;
    OperatorKind op_kind = op_md ? op_md->kind : OP_ASSIGN;

    if (op_kind != OP_ASSIGN && compound_assign_to_binary_op(op_kind) == OP_INVALID)
        return NULL;

    LLVMValueRef rhs_val = ir_gen_node(ctx, node->list.children[2]);
    if (rhs_val == NULL)
        return NULL;

    return ir_gen_assign_store(ctx, node->list.children[0], rhs_val, op_kind, node->list.children[2]);
}

LLVMValueRef
ir_gen_assign_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;

    odin_grammar_node_t * op_node = node_find_op(node);
    AstOpMetadata * op_md = (op_node) ? (AstOpMetadata *)op_node->metadata : NULL;
    OperatorKind op_kind = op_md ? op_md->kind : OP_ASSIGN;

    if (op_kind != OP_ASSIGN && compound_assign_to_binary_op(op_kind) == OP_INVALID)
        return NULL;

    // Find operator index among children
    int op_idx = -1;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (node->list.children[i] == op_node)
        {
            op_idx = (int)i;
            break;
        }
    }
    if (op_idx < 0 || (size_t)(op_idx + 1) >= node->list.count)
        return NULL;

    size_t lhs_count = (size_t)op_idx;
    odin_grammar_node_t * rhs_node = node->list.children[op_idx + 1];

    LLVMValueRef rhs_val = ir_gen_node(ctx, rhs_node);
    if (rhs_val == NULL)
        return NULL;

    // Multi-return destructuring: fd, err = foo()
    if (lhs_count > 1)
    {
        if (op_kind != OP_ASSIGN)
            return NULL;

        for (size_t i = 0; i < lhs_count; i++)
        {
            odin_grammar_node_t * lhs_child = node->list.children[i];
            if (lhs_child == NULL)
                continue;
            LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, lhs_child);
            if (lhs_ptr == NULL)
                continue;
            LLVMValueRef field_val = LLVMBuildExtractValue(ctx->builder, rhs_val, (unsigned)i, "assign.field");
            LLVMBuildStore(ctx->builder, field_val, lhs_ptr);
        }
        return rhs_val;
    }

    return ir_gen_assign_store(ctx, node->list.children[0], rhs_val, op_kind, rhs_node);
}

static LLVMValueRef
ir_gen_assign_store(
    IrGenContext * ctx,
    odin_grammar_node_t * lhs_node,
    LLVMValueRef rhs_val,
    OperatorKind op_kind,
    odin_grammar_node_t * rhs_expr_node
)
{
    if (ir_gen_vector_elem_assign(ctx, lhs_node, rhs_val, op_kind, rhs_expr_node))
        return rhs_val;

    if (ir_gen_bit_field_write(ctx, lhs_node, rhs_val, op_kind))
        return rhs_val;

    if (ir_gen_bit_set_assign_expr(ctx, lhs_node, rhs_expr_node, rhs_val, op_kind))
        return rhs_val;

    LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, lhs_node);
    if (lhs_ptr == NULL)
        return rhs_val;

    LLVMValueRef store_val = rhs_val;
    TypeDescriptor const * lhs_type_desc = NULL;
    odin_grammar_node_t * t = expression_chain_unwrap(lhs_node);
    if (t)
        lhs_type_desc = t->resolved_type;
    if (lhs_type_desc && lhs_type_desc->kind == TD_KIND_BASIC && lhs_type_desc->as.basic.name
        && strcmp(lhs_type_desc->as.basic.name, "any") == 0)
    {
        TypeDescriptor const * rhs_type = rhs_expr_node ? rhs_expr_node->resolved_type : NULL;
        ir_gen_pack_any(ctx, lhs_ptr, rhs_val, lhs_type_desc->llvm_type, rhs_type);
        return rhs_val;
    }

    if (op_kind != OP_ASSIGN)
    {
        OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
        if (lhs_type_desc == NULL)
            lhs_type_desc = type_descriptor_get_int64_type(ctx->type_registry);

        LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, lhs_type_desc->llvm_type, lhs_ptr, "loadtmp");
        store_val = ir_gen_binary_op_by_kind(ctx, lhs_val, rhs_val, bin_op);
    }

    return LLVMBuildStore(ctx->builder, store_val, lhs_ptr);
}

// Defer helpers, statement codegen, and control flow codegen moved to ir_gen_statement.c

