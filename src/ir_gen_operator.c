#include "ir_gen_operator.h"
#include "ast_metadata.h"
#include "ast_utils.h"

// --- Binary expression codegen ---

static LLVMValueRef
ir_gen_logical_short_circuit(IrGenContext * ctx, odin_grammar_node_t * node, OperatorKind op_kind)
{
    LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
    if (lhs == NULL)
        return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);

    LLVMValueRef lhs_bool;
    if (LLVMGetTypeKind(lhs_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(lhs_type) == 1)
        lhs_bool = lhs;
    else
        lhs_bool = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, LLVMConstNull(lhs_type), "log_lhs");

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "logrhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "logmerge");

    if (op_kind == OP_LOG_AND)
        LLVMBuildCondBr(ctx->builder, lhs_bool, rhs_bb, merge_bb);
    else
        LLVMBuildCondBr(ctx->builder, lhs_bool, merge_bb, rhs_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[node->list.count - 1]);
    LLVMValueRef rhs_bool;
    if (rhs != NULL)
    {
        LLVMTypeRef rhs_type = LLVMTypeOf(rhs);
        if (LLVMGetTypeKind(rhs_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(rhs_type) == 1)
            rhs_bool = rhs;
        else
            rhs_bool = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs, LLVMConstNull(rhs_type), "log_rhs");
    }
    else
    {
        rhs_bool = LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, false);
    }
    LLVMBuildBr(ctx->builder, merge_bb);

    // Re-capture the builder's current block: RHS evaluation may have split
    // rhs_bb into multiple blocks (e.g. via ir_gen_emit_bounds_check), so the
    // actual predecessor of merge_bb is now wherever the builder ended up,
    // not the original rhs_bb.
    LLVMBasicBlockRef rhs_end_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, LLVMInt1TypeInContext(ctx->context), "logphi");
    LLVMValueRef lhs_i1 = lhs_bool;
    LLVMBasicBlockRef entry_after_rhs = LLVMGetInsertBlock(ctx->builder);
    if (op_kind == OP_LOG_AND)
    {
        LLVMValueRef phi_vals[2] = {LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, false), rhs_bool};
        LLVMBasicBlockRef phi_blocks[2] = {entry_bb, rhs_end_bb};
        LLVMAddIncoming(phi, phi_vals, phi_blocks, 2);
    }
    else
    {
        LLVMValueRef phi_vals[2] = {LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, false), rhs_bool};
        LLVMBasicBlockRef phi_blocks[2] = {entry_bb, rhs_end_bb};
        LLVMAddIncoming(phi, phi_vals, phi_blocks, 2);
    }

    LLVMValueRef result = LLVMBuildIntCast2(ctx->builder, phi, lhs_type, false, "logext");
    return result;
}

static LLVMValueRef
ir_gen_in_expression(
    IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef lhs, LLVMValueRef rhs, TypeDescriptor const * rhs_type, bool is_not_in
)
{
    if (rhs_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "in/not_in: RHS has no type");
        return NULL;
    }

    LLVMTypeKind rhs_kind = LLVMGetTypeKind(LLVMTypeOf(rhs));

    TypeDescriptor const * elem_type = NULL;
    LLVMValueRef data_ptr = NULL;
    LLVMValueRef count_val = NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);

    if (rhs_type->kind == TD_KIND_SLICE)
    {
        elem_type = rhs_type->element_type;
        if (rhs_kind == LLVMPointerTypeKind)
        {
            LLVMValueRef slice_val = LLVMBuildLoad2(ctx->builder, rhs_type->llvm_type, rhs, "in.slice");
            data_ptr = LLVMBuildExtractValue(ctx->builder, slice_val, 0, "in.data");
            count_val = LLVMBuildExtractValue(ctx->builder, slice_val, 1, "in.len");
        }
        else
        {
            data_ptr = LLVMBuildExtractValue(ctx->builder, rhs, 0, "in.data");
            count_val = LLVMBuildExtractValue(ctx->builder, rhs, 1, "in.len");
        }
    }
    else if (rhs_type->kind == TD_KIND_ARRAY)
    {
        elem_type = rhs_type->element_type;
        if (rhs_kind == LLVMPointerTypeKind)
        {
            data_ptr = LLVMBuildBitCast(
                ctx->builder,
                rhs,
                LLVMPointerType(elem_type ? elem_type->llvm_type : LLVMInt8TypeInContext(ctx->context), 0),
                "in.data"
            );
        }
        else
        {
            data_ptr = LLVMBuildAlloca(ctx->builder, LLVMTypeOf(rhs), "in.arr");
            LLVMBuildStore(ctx->builder, rhs, data_ptr);
        }
        count_val = LLVMConstInt(i64, (unsigned long long)rhs_type->as.array.count, false);
    }
    else if (rhs_type->kind == TD_KIND_BIT_SET)
    {
        LLVMValueRef backing = rhs;
        LLVMTypeRef backing_type = rhs_type->llvm_type;
        if (LLVMGetTypeKind(LLVMTypeOf(rhs)) == LLVMPointerTypeKind)
        {
            backing = LLVMBuildLoad2(ctx->builder, backing_type, rhs, "bs.load");
        }
        LLVMValueRef lhs_cast = LLVMBuildIntCast(ctx->builder, lhs, backing_type, "bs.shift");
        LLVMValueRef shifted = LLVMBuildLShr(ctx->builder, backing, lhs_cast, "bs.shifted");
        LLVMValueRef result = LLVMBuildAnd(ctx->builder, shifted, LLVMConstInt(backing_type, 1, false), "bs.result");
        if (is_not_in)
            result = LLVMBuildICmp(ctx->builder, LLVMIntEQ, result, LLVMConstNull(backing_type), "bs.notin");
        else
            result = LLVMBuildICmp(ctx->builder, LLVMIntNE, result, LLVMConstNull(backing_type), "bs.in");
        LLVMValueRef zext = LLVMBuildZExt(ctx->builder, result, LLVMInt64TypeInContext(ctx->context), "bs.ext");
        return zext;
    }
    else
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "in/not_in: unsupported container type");
        return NULL;
    }

    if (elem_type == NULL || data_ptr == NULL || count_val == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "in/not_in: failed to extract element type, data, or count from container");
        return NULL;
    }

    LLVMValueRef func = func_current_function(ctx);

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);

    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.loop");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.body");
    LLVMBasicBlockRef incr_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.incr");
    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.found");
    LLVMBasicBlockRef notfound_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.notfound");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "in.merge");

    LLVMMoveBasicBlockAfter(loop_bb, entry_bb);
    LLVMMoveBasicBlockAfter(body_bb, loop_bb);
    LLVMMoveBasicBlockAfter(incr_bb, body_bb);
    LLVMMoveBasicBlockAfter(found_bb, incr_bb);
    LLVMMoveBasicBlockAfter(notfound_bb, found_bb);
    LLVMMoveBasicBlockAfter(merge_bb, notfound_bb);

    LLVMValueRef idx_alloca = LLVMBuildAlloca(ctx->builder, i64, "in.idx");
    LLVMBuildStore(ctx->builder, LLVMConstNull(i64), idx_alloca);
    LLVMBuildBr(ctx->builder, loop_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, loop_bb);
    LLVMValueRef idx = LLVMBuildLoad2(ctx->builder, i64, idx_alloca, "in.idx");
    LLVMValueRef loop_cond = LLVMBuildICmp(ctx->builder, LLVMIntSLT, idx, count_val, "in.loop.cond");
    LLVMBuildCondBr(ctx->builder, loop_cond, body_bb, notfound_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, elem_type->llvm_type, data_ptr, &idx, 1, "in.elem");
    LLVMValueRef elem_val = LLVMBuildLoad2(ctx->builder, elem_type->llvm_type, elem_ptr, "in.elem.val");

    LLVMTypeKind elem_kind = LLVMGetTypeKind(elem_type->llvm_type);
    LLVMValueRef eq;
    if (elem_kind == LLVMFloatTypeKind || elem_kind == LLVMDoubleTypeKind)
        eq = LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs, elem_val, "in.eq");
    else
        eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, elem_val, "in.eq");

    LLVMBuildCondBr(ctx->builder, eq, found_bb, incr_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, incr_bb);
    LLVMValueRef next_idx = LLVMBuildAdd(ctx->builder, idx, LLVMConstInt(i64, 1, false), "in.next");
    LLVMBuildStore(ctx->builder, next_idx, idx_alloca);
    LLVMBuildBr(ctx->builder, loop_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, found_bb);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, notfound_bb);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef result_i1 = LLVMBuildPhi(ctx->builder, LLVMInt1TypeInContext(ctx->context), "in.result");
    LLVMValueRef phi_true = LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, false);
    LLVMValueRef phi_false = LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 0, false);
    LLVMBasicBlockRef phi_blocks[2] = {found_bb, notfound_bb};
    LLVMValueRef phi_vals[2] = {phi_true, phi_false};
    LLVMAddIncoming(result_i1, phi_vals, phi_blocks, 2);

    if (is_not_in)
    {
        LLVMValueRef not_val = LLVMBuildNot(ctx->builder, result_i1, "in.not");
        return LLVMBuildIntCast2(ctx->builder, not_val, LLVMInt64TypeInContext(ctx->context), false, "in.ext");
    }
    return LLVMBuildIntCast2(ctx->builder, result_i1, LLVMInt64TypeInContext(ctx->context), false, "in.ext");
}

LLVMValueRef
ir_gen_binary_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node == NULL)
    {
        // Single operand — chainl1 wrapper, recurse into first child
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    if (node->list.count < 3)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "binary expression has too few children");
        return NULL;
    }

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "binary expression missing operator metadata");
        return NULL;
    }

    if (op_md->kind == OP_LOG_AND || op_md->kind == OP_LOG_OR)
        return ir_gen_logical_short_circuit(ctx, node, op_md->kind);

    LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[node->list.count - 1]);
    if (lhs == NULL || rhs == NULL)
        return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
    LLVMTypeRef rhs_type = LLVMTypeOf(rhs);

    // Implicit type coercion for arithmetic/comparison ops only
    if (lhs_type != rhs_type && op_md->kind != OP_IN && op_md->kind != OP_NOT_IN && op_md->kind != OP_RANGE
        && op_md->kind != OP_RANGE_HALF)
    {
        rhs = coerce_value_to_type(ctx, rhs, lhs_type, false, "coerce");
    }

    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float = (type_kind == LLVMHalfTypeKind || type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (op_md->kind)
    {
    case OP_ADD:
        return is_float ? LLVMBuildFAdd(ctx->builder, lhs, rhs, "addtmp")
                        : LLVMBuildAdd(ctx->builder, lhs, rhs, "addtmp");
    case OP_SUB:
    {
        // bit_set - bit_set is AND-NOT (set difference), not integer subtraction
        TypeDescriptor const * lhs_td = node->list.children[0]->resolved_type;
        TypeDescriptor const * rhs_td = node->list.children[node->list.count - 1]->resolved_type;
        if (lhs_td && lhs_td->kind == TD_KIND_BIT_SET && rhs_td && rhs_td->kind == TD_KIND_BIT_SET)
        {
            return LLVMBuildAnd(ctx->builder, lhs, LLVMBuildNot(ctx->builder, rhs, "bs.not"), "bs.diff");
        }
        return is_float ? LLVMBuildFSub(ctx->builder, lhs, rhs, "subtmp")
                        : LLVMBuildSub(ctx->builder, lhs, rhs, "subtmp");
    }
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
    case OP_EQ:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_NE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_LT:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_GT:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_LE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_GE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs, rhs, "cmptmp");
        return cmp;
    }
    case OP_LOG_AND:
    {
        LLVMValueRef l = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, LLVMConstNull(lhs_type), "land_lhs");
        LLVMValueRef r = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs, LLVMConstNull(lhs_type), "land_rhs");
        LLVMValueRef and_val = LLVMBuildAnd(ctx->builder, l, r, "landtmp");
        return LLVMBuildIntCast2(ctx->builder, and_val, lhs_type, false, "landext");
    }
    case OP_LOG_OR:
    {
        LLVMValueRef l = LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, LLVMConstNull(lhs_type), "lor_lhs");
        LLVMValueRef r = LLVMBuildICmp(ctx->builder, LLVMIntNE, rhs, LLVMConstNull(lhs_type), "lor_rhs");
        LLVMValueRef or_val = LLVMBuildOr(ctx->builder, l, r, "lortmp");
        return LLVMBuildIntCast2(ctx->builder, or_val, lhs_type, false, "lorext");
    }
    case OP_IN:
    {
        TypeDescriptor const * rhs_type = node->list.children[node->list.count - 1]->resolved_type;
        return ir_gen_in_expression(ctx, node, lhs, rhs, rhs_type, false);
    }
    case OP_NOT_IN:
    {
        TypeDescriptor const * rhs_type = node->list.children[node->list.count - 1]->resolved_type;
        return ir_gen_in_expression(ctx, node, lhs, rhs, rhs_type, true);
    }
    case OP_RANGE:
    case OP_RANGE_HALF:
    {
        bool inclusive = (op_md->kind == OP_RANGE);
        TypeDescriptor const * range_desc = node->resolved_type;
        LLVMTypeRef range_struct = range_desc->llvm_type;
        LLVMValueRef range_alloca = LLVMBuildAlloca(ctx->builder, range_struct, "range");
        LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef idx_low[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMValueRef low_field
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, idx_low, 2, "range.low.gep");
        LLVMBuildStore(ctx->builder, lhs, low_field);
        LLVMValueRef high;
        if (inclusive)
        {
            LLVMTypeRef rhs_llvm_type = LLVMTypeOf(rhs);
            high = LLVMBuildAdd(ctx->builder, rhs, LLVMConstInt(rhs_llvm_type, 1, false), "range.high.inc");
        }
        else
        {
            high = rhs;
        }
        LLVMValueRef idx_high[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMValueRef high_field
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, idx_high, 2, "range.high.gep");
        LLVMBuildStore(ctx->builder, high, high_field);
        LLVMValueRef range_val = LLVMBuildLoad2(ctx->builder, range_struct, range_alloca, "range.val");
        return range_val;
    }
    default:
        return NULL;
    }

#pragma GCC diagnostic pop
}

// --- Unary expression codegen ---

LLVMValueRef
ir_gen_unary_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // UnaryExpression = UnaryPrefix | PostfixExpression
    // UnaryPrefix = (UnaryOp UnaryExpression) @AST_ACTION_UNARY_PREFIX
    odin_grammar_node_t * op_node = node_find_child(node, AST_NODE_UNARY_OP);
    if (op_node == NULL)
    {
        // It's a PostfixExpression — recurse
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "unary expression missing operator metadata");
        return NULL;
    }

    // Find the operand child (the expression after the UnaryOp)
    odin_grammar_node_t * operand_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && child != op_node)
        {
            operand_node = child;
            break;
        }
    }
    if (operand_node == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "unary expression has no operand");
        return NULL;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (op_md->kind)
    {
    case OP_UNARY_ADDR:
    {
        LLVMValueRef ptr = ir_gen_lvalue(ctx, operand_node);
        if (ptr == NULL)
        {
            odin_grammar_node_t * ident = expression_unwrap_to_identifier(operand_node);
            if (ident)
            {
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
                if (sym && sym->value.is_lvalue)
                    ptr = sym->value.value;
            }
        }
        return ptr;
    }

    case OP_UNARY_DEREF:
    {
        LLVMValueRef operand = ir_gen_node(ctx, operand_node);
        if (operand == NULL)
            return NULL;
        LLVMTypeRef ptr_type = LLVMTypeOf(operand);
        TypeDescriptor const * td = operand_node->resolved_type;
        if (td && (td->kind == TD_KIND_POINTER || td->kind == TD_KIND_MULTI_POINTER) && td->pointee)
        {
            return LLVMBuildLoad2(ctx->builder, td->pointee->llvm_type, operand, "deref");
        }
        return LLVMBuildLoad2(ctx->builder, LLVMGetElementType(ptr_type), operand, "deref");
    }

    default:
    {
        LLVMValueRef operand = ir_gen_node(ctx, operand_node);
        if (operand == NULL)
            return NULL;

        LLVMTypeRef operand_type = LLVMTypeOf(operand);
        LLVMTypeKind type_kind = LLVMGetTypeKind(operand_type);
        bool is_float = (type_kind == LLVMHalfTypeKind || type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

        switch (op_md->kind)
        {
        case OP_UNARY_NEG:
            return is_float ? LLVMBuildFNeg(ctx->builder, operand, "negtmp")
                            : LLVMBuildNeg(ctx->builder, operand, "negtmp");
        case OP_UNARY_POS:
            return operand;
        case OP_UNARY_NOT:
        {
            LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(operand));
            LLVMValueRef is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand, zero, "iszero");
            return LLVMBuildIntCast2(ctx->builder, is_zero, LLVMTypeOf(operand), false, "lognot");
        }
        case OP_UNARY_XOR:
            return LLVMBuildNot(ctx->builder, operand, "xortmp");
        default:
            return NULL;
        }
    }
    }

#pragma GCC diagnostic pop
}
