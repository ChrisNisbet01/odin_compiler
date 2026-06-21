#include "llvm_ir_generator.h"

#include "ast_metadata.h"
#include "ast_utils.h"
#include "ir_utils.h"
#include "operator_kind.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Forward declarations ---
static LLVMValueRef ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_lvalue(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_nested_procedure_decl(IrGenContext * ctx, odin_grammar_node_t * node);
static odin_grammar_node_t * expression_unwrap_to_identifier(odin_grammar_node_t * node);

static void ir_gen_emit_defers_at_depth(IrGenContext * ctx, int depth);
static void ir_gen_emit_defers_from_depth(IrGenContext * ctx, int min_depth);
static void ir_gen_emit_all_defers(IrGenContext * ctx);
static LLVMValueRef ir_gen_or_else_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_or_return_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_ternary_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_in_expression(
    IrGenContext * ctx, LLVMValueRef lhs, LLVMValueRef rhs, TypeDescriptor const * rhs_type, bool is_not_in
);

// --- Context lifecycle ---

IrGenContext *
ir_gen_context_create(char const * module_name, TypeDescriptors * type_registry, GeneratorContext * gen_ctx)
{
    IrGenContext * ctx = calloc(1, sizeof(IrGenContext));
    if (ctx == NULL)
        return NULL;

    ctx->context = gen_ctx->context;
    ctx->builder = gen_ctx->builder;

    ctx->module = LLVMModuleCreateWithNameInContext(module_name, ctx->context);
    if (ctx->module == NULL)
    {
        free(ctx);
        return NULL;
    }

    // Set a default data layout on the module
    {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        char const * triple = LLVMGetDefaultTargetTriple();
        LLVMTargetRef target = NULL;
        char * error = NULL;
        if (LLVMGetTargetFromTriple(triple, &target, &error) == 0)
        {
            LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
                target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
            );
            LLVMTargetDataRef target_dl = LLVMCreateTargetDataLayout(tm);
            char * dl_str = LLVMCopyStringRepOfTargetData(target_dl);
            LLVMSetDataLayout(ctx->module, dl_str);
            LLVMDisposeMessage(dl_str);
            LLVMDisposeTargetData(target_dl);
            LLVMDisposeTargetMachine(tm);
        }
        else
        {
            LLVMDisposeMessage(error);
        }
    }
    ctx->data_layout = LLVMGetModuleDataLayout(ctx->module);
    ctx->type_registry = type_registry;
    ctx->gen_ctx = gen_ctx;
    ctx->anon_counter = 0;
    ctx->loop_depth = 0;
    ir_gen_error_collection_init(&ctx->errors);

    return ctx;
}

void
ir_gen_context_destroy(IrGenContext * ctx)
{
    if (ctx == NULL)
        return;
    // Note: builder and context are owned by main.c (via gen_ctx), not disposed
    // here
    if (ctx->module)
        LLVMDisposeModule(ctx->module);
    free(ctx);
}

// --- Expression codegen ---

static LLVMValueRef
ir_gen_integer_value(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return NULL;

    TypeDescriptor const * type_desc = node->resolved_type;

    LLVMTypeRef llvm_type = type_desc ? type_desc->llvm_type : LLVMInt64TypeInContext(ctx->context);

    char * endptr = NULL;
    unsigned long long val = strtoull(node->text, &endptr, 0);
    return LLVMConstInt(llvm_type, val, false);
}

static LLVMValueRef
ir_gen_identifier(IrGenContext * ctx, odin_grammar_node_t * node)
{
    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
    if (sym == NULL)
    {
        sym = node->resolved_symbol;
    }

    if (sym == NULL)
        return NULL;

    if (sym->value.is_lvalue && sym->value.value != NULL)
    {
        // Don't load composite types — the pointer is needed for
        // GEP/subscript/member access
        if (sym->value.type_info
            && (sym->value.type_info->kind == TD_KIND_ARRAY || sym->value.type_info->kind == TD_KIND_SLICE
                || sym->value.type_info->kind == TD_KIND_STRUCT || sym->value.type_info->kind == TD_KIND_DYNAMIC_ARRAY
                || sym->value.type_info->kind == TD_KIND_MAP))
        {
            return sym->value.value;
        }

        // Load from alloca to get the value
        // Use the type from type_info (not LLVMGetElementType, which breaks with
        // opaque pointers)
        LLVMTypeRef elem_type = sym->value.type_info ? sym->value.type_info->llvm_type : NULL;
        if (elem_type == NULL)
        {
            return sym->value.value;
        }
        LLVMValueRef load = LLVMBuildLoad2(ctx->builder, elem_type, sym->value.value, node->text);
        LLVMSetAlignment(load, LLVMABIAlignmentOfType(ctx->data_layout, elem_type));
        return load;
    }

    return sym->value.value;
}

static LLVMValueRef
ir_gen_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

    if (node->list.count >= 1 && node->list.children[0] != NULL)
    {
        return ir_gen_node(ctx, node->list.children[0]);
    }

    return NULL;
}

// --- Literal codegen ---

static LLVMValueRef
ir_gen_float_value(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return NULL;
    TypeDescriptor const * type_desc = node->resolved_type;
    LLVMTypeRef llvm_type = type_desc ? type_desc->llvm_type : LLVMDoubleTypeInContext(ctx->context);
    char * endptr = NULL;
    double val = strtod(node->text, &endptr);
    return LLVMConstReal(llvm_type, val);
}

static LLVMValueRef
ir_gen_bool_value(IrGenContext * ctx, odin_grammar_node_t * node)
{
    (void)ctx;
    bool val = (node->type == AST_NODE_BOOL_TRUE);
    return LLVMConstInt(LLVMInt1TypeInContext(ctx->context), val ? 1 : 0, false);
}

static LLVMValueRef
ir_gen_string_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return LLVMConstNull(LLVMStructType(NULL, 0, false));

    char const * text = node->text;
    size_t text_len = strlen(text);

    // Strip surrounding quotes/backticks
    char const * content = text;
    size_t content_len = text_len;
    if (text_len >= 2 && (text[0] == '"' || text[0] == '`'))
    {
        content = text + 1;
        content_len = text_len - 2;
    }

    // Build [N x i8] constant (with null terminator)
    size_t arr_len = content_len + 1;
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    LLVMValueRef * elements = malloc(arr_len * sizeof(LLVMValueRef));
    if (elements == NULL)
        return NULL;

    for (size_t i = 0; i < content_len; i++)
        elements[i] = LLVMConstInt(i8_type, (unsigned char)content[i], false);
    elements[content_len] = LLVMConstInt(i8_type, 0, false); // null terminator

    LLVMTypeRef arr_type = LLVMArrayType(i8_type, arr_len);
    LLVMValueRef arr_const = LLVMConstArray(i8_type, elements, arr_len);
    free(elements);

    // Private global constant
    LLVMValueRef global = LLVMAddGlobal(ctx->module, arr_type, ".str");
    LLVMSetInitializer(global, arr_const);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
    LLVMSetGlobalConstant(global, true);

    // GEP to i8* pointer to first element (constant expression)
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    LLVMValueRef indices[] = {zero, zero};
    LLVMValueRef ptr = LLVMConstInBoundsGEP2(arr_type, global, indices, 2);

    // Build {i8*, i64} string struct as a constant
    TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
    LLVMTypeRef str_type = str_desc ? str_desc->llvm_type : LLVMStructType(NULL, 0, false);

    LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), content_len, false);
    LLVMValueRef struct_vals[] = {ptr, len_val};
    LLVMValueRef str_val = LLVMConstNamedStruct(str_type, struct_vals, 2);

    return str_val;
}

static LLVMValueRef
ir_gen_rune_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return NULL;
    // For now, treat rune as i32 value
    char * endptr = NULL;
    unsigned long long val = 0;
    if (node->text[0] == '\'' && node->text[1] != '\\')
    {
        // Simple character: 'a'
        val = (unsigned char)node->text[1];
    }
    else
    {
        val = strtoull(node->text + 2, &endptr, 0);
    }
    return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), val, false);
}

static LLVMValueRef
ir_gen_nil(IrGenContext * ctx, odin_grammar_node_t * node)
{
    (void)node;
    return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
}

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
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "logrhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "logmerge");

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

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMTypeRef bool_ty = LLVMInt1TypeInContext(ctx->context);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, bool_ty, "log_phi");

    if (op_kind == OP_LOG_AND)
    {
        LLVMValueRef incoming_vals[] = {LLVMConstInt(bool_ty, 0, false), rhs_bool};
        LLVMBasicBlockRef incoming_blocks[] = {entry_bb, rhs_bb};
        LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    }
    else
    {
        LLVMValueRef incoming_vals[] = {LLVMConstInt(bool_ty, 1, false), rhs_bool};
        LLVMBasicBlockRef incoming_blocks[] = {entry_bb, rhs_bb};
        LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    }

    return LLVMBuildIntCast2(ctx->builder, phi, lhs_type, false, "logext");
}

static LLVMValueRef
ir_gen_in_expression(
    IrGenContext * ctx, LLVMValueRef lhs, LLVMValueRef rhs, TypeDescriptor const * rhs_type, bool is_not_in
)
{
    if (rhs_type == NULL)
        return NULL;

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
    else
    {
        return NULL;
    }

    if (elem_type == NULL || data_ptr == NULL || count_val == NULL)
        return NULL;

    LLVMValueRef func = ctx->current_function;

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

static LLVMValueRef
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
        return NULL;

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL)
        return NULL;

    if (op_md->kind == OP_LOG_AND || op_md->kind == OP_LOG_OR)
        return ir_gen_logical_short_circuit(ctx, node, op_md->kind);

    LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[node->list.count - 1]);
    if (lhs == NULL || rhs == NULL)
        return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (op_md->kind)
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
    case OP_EQ:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
    }
    case OP_NE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
    }
    case OP_LT:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
    }
    case OP_GT:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
    }
    case OP_LE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
    }
    case OP_GE:
    {
        LLVMValueRef cmp = is_float ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs, rhs, "cmptmp")
                                    : LLVMBuildICmp(ctx->builder, LLVMIntSGE, lhs, rhs, "cmptmp");
        return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
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
        return ir_gen_in_expression(ctx, lhs, rhs, rhs_type, false);
    }
    case OP_NOT_IN:
    {
        TypeDescriptor const * rhs_type = node->list.children[node->list.count - 1]->resolved_type;
        return ir_gen_in_expression(ctx, lhs, rhs, rhs_type, true);
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

static LLVMValueRef
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
        return NULL;

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
        return NULL;

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
        if (td && td->kind == TD_KIND_POINTER && td->pointee)
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
        bool is_float = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

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

// --- Enum type codegen ---

static void
ir_gen_register_enum_enumerators(IrGenContext * ctx, odin_grammar_node_t * enum_type_node)
{
    odin_grammar_node_t * enumerator_list = NULL;
    for (size_t i = 0; i < enum_type_node->list.count; i++)
    {
        odin_grammar_node_t * child = enum_type_node->list.children[i];
        if (child && child->type == AST_NODE_ENUMERATOR_LIST)
        {
            enumerator_list = child;
            break;
        }
    }
    if (enumerator_list == NULL)
        return;

    TypeDescriptor const * enum_td = enum_type_node->resolved_type;
    LLVMTypeRef llvm_int_type = LLVMInt64TypeInContext(ctx->context);
    if (enum_td && enum_td->llvm_type)
        llvm_int_type = enum_td->llvm_type;

    int next_value = 0;
    for (size_t i = 0; i < enumerator_list->list.count; i++)
    {
        odin_grammar_node_t * enumerator = enumerator_list->list.children[i];
        if (enumerator == NULL || enumerator->type != AST_NODE_ENUMERATOR)
            continue;

        odin_grammar_node_t * en_name_node = NULL;
        odin_grammar_node_t * en_value_node = NULL;
        for (size_t ci = 0; ci < enumerator->list.count; ci++)
        {
            odin_grammar_node_t * child = enumerator->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_IDENTIFIER)
                en_name_node = child;
            else
                en_value_node = child;
        }
        if (en_name_node == NULL || en_name_node->text == NULL)
            continue;

        int value = next_value;
        if (en_value_node)
        {
            LLVMValueRef val = ir_gen_node(ctx, en_value_node);
            if (val && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind)
                value = (int)LLVMConstIntGetSExtValue(val);
        }

        LLVMValueRef llvm_val = LLVMConstInt(llvm_int_type, (unsigned long long)value, false);
        TypedValue tv = create_typed_value(llvm_val, enum_td, false);
        scope_add_symbol(generator_current_scope(ctx->gen_ctx), en_name_node->text, tv);

        next_value = value + 1;
    }
}

// --- Variable codegen ---

static LLVMValueRef
ir_gen_variable_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    TypeDescriptor const * var_type = node->resolved_type;
    if (var_type == NULL)
    {
        // Try to get type from type annotation child
        for (size_t i = 1; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && child->resolved_type)
            {
                var_type = child->resolved_type;
                break;
            }
        }
    }
    // Fallback: try type of second child resolved_type
    if (var_type == NULL && node->list.count >= 2 && node->list.children[1])
    {
        var_type = node->list.children[1]->resolved_type;
    }
    if (var_type == NULL)
    {
        var_type = type_descriptor_get_int64_type(ctx->type_registry);
    }

    LLVMTypeRef llvm_type = var_type->llvm_type;
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, llvm_type, name_node->text);
    LLVMSetAlignment(alloca, LLVMABIAlignmentOfType(ctx->data_layout, llvm_type));

    // Store initial value if present (third child for `x: type = expr`)
    if (node->list.count >= 3)
    {
        odin_grammar_node_t * init_node = node->list.children[2];
        if (init_node)
        {
            LLVMValueRef init_val = ir_gen_node(ctx, init_node);
            if (init_val)
            {
                // If variable is of type `any`, pack the value into the {i8*, i64} struct
                if (var_type && var_type->as.basic.name && strcmp(var_type->as.basic.name, "any") == 0)
                {
                    // Build a runtime any struct {i8*, i64} using GEP + store
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                    LLVMValueRef zero_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    LLVMValueRef data_ptr;
                    LLVMTypeRef val_type = LLVMTypeOf(init_val);
                    LLVMTypeKind val_kind = LLVMGetTypeKind(val_type);
                    if (val_kind == LLVMIntegerTypeKind)
                    {
                        data_ptr = LLVMBuildIntToPtr(ctx->builder, init_val, i8ptr, "anydata");
                    }
                    else if (val_kind == LLVMPointerTypeKind)
                    {
                        data_ptr = LLVMBuildBitCast(ctx->builder, init_val, i8ptr, "anydata");
                    }
                    else
                    {
                        // For struct/array types, allocate temporary storage
                        LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, val_type, "anytmp");
                        LLVMBuildStore(ctx->builder, init_val, tmp);
                        data_ptr = LLVMBuildBitCast(ctx->builder, tmp, i8ptr, "anydata");
                    }
                    LLVMValueRef type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
                    LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    LLVMValueRef gep0[2] = {zero_idx, idx1};
                    LLVMValueRef data_field
                        = LLVMBuildInBoundsGEP2(ctx->builder, var_type->llvm_type, alloca, gep0, 2, "any.data");
                    LLVMBuildStore(ctx->builder, data_ptr, data_field);
                    LLVMValueRef idx2 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
                    LLVMValueRef gep1[2] = {zero_idx, idx2};
                    LLVMValueRef id_field
                        = LLVMBuildInBoundsGEP2(ctx->builder, var_type->llvm_type, alloca, gep1, 2, "any.typeid");
                    LLVMBuildStore(ctx->builder, type_id, id_field);
                }
                else
                {
                    LLVMBuildStore(ctx->builder, init_val, alloca);
                }
            }
        }
    }
    // For `x := expr`, the initializer is at children[1]
    else if (node->list.count == 2)
    {
        odin_grammar_node_t * second = node->list.children[1];
        if (second && second->type != AST_NODE_BASIC_TYPE)
        {
            LLVMValueRef init_val = ir_gen_node(ctx, second);
            if (init_val)
            {
                LLVMBuildStore(ctx->builder, init_val, alloca);
            }
        }
    }

    // Register in current scope
    TypedValue tv = create_typed_value(alloca, var_type, true);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

    // If the type annotation is an enum type, register its enumerators
    // in the current scope so they can be looked up during IR gen
    // (the semantic analysis scope was freed, so resolved_symbol is dangling)
    for (size_t i = 1; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child && child->type == AST_NODE_ENUM_TYPE)
        {
            ir_gen_register_enum_enumerators(ctx, child);
            break;
        }
    }

    return alloca;
}

// --- Assignment codegen ---

// Recursively unwrap expression wrapper nodes to find the identifier child.
// Wrapper nodes simply delegate to their first child.
static bool
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

static odin_grammar_node_t *
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

    // Parse text to determine which bounds are present.
    // Text is "[i..j]", "[i..]", "[..j]", "[..]" for SLICE
    // or "[i..<j]" etc for SLICE_LT.
    char const * text = op->text;
    size_t len = strlen(text);

    // Find the range separator
    char const * sep = strstr(text, "..");
    if (sep != NULL)
    {
        size_t sep_pos = sep - text;
        size_t sep_len = 2;
        if (sep_len + 1 < len && text[sep_pos + 2] == '<')
            sep_len = 3;

        // Content between '[' and sep indicates low bound
        if (sep_pos > 1)
            info.has_low = 1;

        // Content after sep and before ']' indicates high bound
        char const * after = sep + sep_len;
        size_t after_len = len - (size_t)(after - text);
        if (after_len > 1)
            info.has_high = 1;
    }

    // Collect expression children in order
    odin_grammar_node_t * exprs[2] = {NULL, NULL};
    int expr_count = 0;
    for (size_t i = 0; i < op->list.count; i++)
    {
        odin_grammar_node_t * child = op->list.children[i];
        if (child != NULL && is_expression_wrapper_type(child->type))
        {
            if (expr_count < 2)
                exprs[expr_count] = child;
            expr_count++;
        }
    }

    if (expr_count == 2)
    {
        info.low_expr = exprs[0];
        info.high_expr = exprs[1];
    }
    else if (expr_count == 1)
    {
        if (info.has_low && !info.has_high)
            info.low_expr = exprs[0];
        else if (info.has_high && !info.has_low)
            info.high_expr = exprs[0];
    }

    return info;
}

// Evaluate a node as an lvalue (return a pointer to the storage location).
static LLVMValueRef
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

        for (size_t i = 0; i < postfix_ops->list.count; i++)
        {
            odin_grammar_node_t * op = postfix_ops->list.children[i];
            if (op == NULL)
                continue;

            switch (op->type)
            {
            case AST_NODE_POSTFIX_SUBSCRIPT:
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
                    return NULL;

                LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
                if (index_val == NULL)
                    return NULL;

                if (cur_type == NULL)
                    return NULL;

                if (cur_type->kind == TD_KIND_ARRAY)
                {
                    LLVMTypeRef arr_type = cur_type->llvm_type;
                    LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false), index_val};
                    ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, ptr, indices, 2, "subs");

                    if (cur_type->element_type)
                        cur_type = cur_type->element_type;
                }
                else if (cur_type->kind == TD_KIND_SLICE || cur_type->kind == TD_KIND_DYNAMIC_ARRAY)
                {
                    LLVMValueRef data_indices[]
                        = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                           LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                    LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
                        ctx->builder, cur_type->llvm_type, ptr, data_indices, 2, "slice.data.ptr"
                    );
                    LLVMValueRef data = LLVMBuildLoad2(
                        ctx->builder, LLVMPointerType(cur_type->element_type->llvm_type, 0), data_field, "slice.data"
                    );

                    ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, cur_type->element_type->llvm_type, data, &index_val, 1, "slice.subs"
                    );

                    if (cur_type->element_type)
                        cur_type = cur_type->element_type;
                }
                else if (cur_type->kind == TD_KIND_MAP)
                {
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
                    LLVMTypeRef i8t = LLVMInt8TypeInContext(ctx->context);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(ctx->context);
                    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, false);
                    LLVMValueRef one64 = LLVMConstInt(i64t, 1, false);
                    LLVMValueRef zero32 = LLVMConstInt(i32t, 0, false);

                    TypeDescriptor const * key_td = cur_type->as.map.key_type;
                    TypeDescriptor const * val_td = cur_type->as.map.value_type;

                    LLVMValueRef didx[] = {zero32, zero32};
                    LLVMValueRef data_gep
                        = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, ptr, didx, 2, "map.dgep");
                    LLVMValueRef data_ptr = LLVMBuildLoad2(ctx->builder, LLVMPointerType(i8t, 0), data_gep, "map.data");

                    LLVMValueRef cap_off = LLVMConstInt(i64t, 8, false);
                    LLVMValueRef cap_ptr = LLVMBuildPointerCast(
                        ctx->builder,
                        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &cap_off, 1, "map.cap.gep"),
                        LLVMPointerType(i64t, 0),
                        ""
                    );
                    LLVMValueRef cap_val = LLVMBuildLoad2(ctx->builder, i64t, cap_ptr, "map.cap");

                    LLVMValueRef ks_off = LLVMConstInt(i64t, 16, false);
                    LLVMValueRef ks_ptr = LLVMBuildPointerCast(
                        ctx->builder,
                        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &ks_off, 1, "map.ks.gep"),
                        LLVMPointerType(i64t, 0),
                        ""
                    );
                    LLVMValueRef key_sz = LLVMBuildLoad2(ctx->builder, i64t, ks_ptr, "map.ks");

                    LLVMValueRef vs_off = LLVMConstInt(i64t, 24, false);
                    LLVMValueRef vs_ptr = LLVMBuildPointerCast(
                        ctx->builder,
                        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &vs_off, 1, "map.vs.gep"),
                        LLVMPointerType(i64t, 0),
                        ""
                    );
                    LLVMValueRef val_sz = LLVMBuildLoad2(ctx->builder, i64t, vs_ptr, "map.vs");

                    LLVMValueRef hdr32 = LLVMConstInt(i64t, 32, false);
                    LLVMValueRef entries_base
                        = LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &hdr32, 1, "map.ents");

                    LLVMValueRef stride = LLVMBuildAdd(
                        ctx->builder, one64, LLVMBuildAdd(ctx->builder, key_sz, val_sz, "kvsz"), "map.stride"
                    );
                    LLVMValueRef ks_plus_one = LLVMBuildAdd(ctx->builder, key_sz, one64, "map.ksp1");

                    LLVMValueRef key_to_compare
                        = LLVMBuildIntCast(ctx->builder, index_val, key_td->llvm_type, "map.key.cast");

                    LLVMValueRef res_alloca
                        = LLVMBuildAlloca(ctx->builder, LLVMPointerType(val_td->llvm_type, 0), "m.res.alloca");
                    LLVMBuildStore(ctx->builder, LLVMConstNull(LLVMPointerType(val_td->llvm_type, 0)), res_alloca);

                    LLVMValueRef fe_alloca = LLVMBuildAlloca(ctx->builder, i64t, "m.fe.alloca");
                    LLVMBuildStore(ctx->builder, cap_val, fe_alloca);

                    LLVMValueRef cnt_off = LLVMConstInt(i64t, 0, false);
                    LLVMValueRef cnt_ptr = LLVMBuildPointerCast(
                        ctx->builder,
                        LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &cnt_off, 1, "map.cnt.gep"),
                        LLVMPointerType(i64t, 0),
                        ""
                    );

                    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
                    LLVMBasicBlockRef loop_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.loop");
                    LLVMBasicBlockRef body_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.body");
                    LLVMBasicBlockRef kchk_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.kchk");
                    LLVMBasicBlockRef found_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.found");
                    LLVMBasicBlockRef next_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.next");
                    LLVMBasicBlockRef empty_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.empty");
                    LLVMBasicBlockRef after_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.after");
                    LLVMBasicBlockRef claim_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.claim");
                    LLVMBasicBlockRef merge_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "m.merge");

                    LLVMBuildBr(ctx->builder, loop_bb);
                    LLVMPositionBuilderAtEnd(ctx->builder, loop_bb);
                    LLVMValueRef i_phi = LLVMBuildPhi(ctx->builder, i64t, "m.i");
                    LLVMValueRef loop_cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, i_phi, cap_val, "m.lcmp");
                    LLVMBuildCondBr(ctx->builder, loop_cmp, body_bb, after_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
                    LLVMValueRef ioff = LLVMBuildMul(ctx->builder, i_phi, stride, "m.ioff");
                    LLVMValueRef entry_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entries_base, &ioff, 1, "m.ep");
                    LLVMValueRef occupied = LLVMBuildLoad2(ctx->builder, i8t, entry_ptr, "m.occ");
                    LLVMValueRef occ_cmp
                        = LLVMBuildICmp(ctx->builder, LLVMIntNE, occupied, LLVMConstNull(i8t), "m.occmp");
                    LLVMBuildCondBr(ctx->builder, occ_cmp, kchk_bb, empty_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, kchk_bb);
                    LLVMValueRef key_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &one64, 1, "m.kp");
                    LLVMValueRef kp_typed
                        = LLVMBuildPointerCast(ctx->builder, key_ptr, LLVMPointerType(key_td->llvm_type, 0), "");
                    LLVMValueRef loaded_key = LLVMBuildLoad2(ctx->builder, key_td->llvm_type, kp_typed, "m.lk");
                    LLVMValueRef key_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, key_to_compare, loaded_key, "m.keq");
                    LLVMBuildCondBr(ctx->builder, key_eq, found_bb, next_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, found_bb);
                    LLVMValueRef val_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &ks_plus_one, 1, "m.vp");
                    LLVMValueRef val_ptr_typed
                        = LLVMBuildPointerCast(ctx->builder, val_ptr, LLVMPointerType(val_td->llvm_type, 0), "");
                    LLVMBuildStore(ctx->builder, val_ptr_typed, res_alloca);
                    LLVMBuildBr(ctx->builder, merge_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, empty_bb);
                    LLVMValueRef cur_fe = LLVMBuildLoad2(ctx->builder, i64t, fe_alloca, "m.curfe");
                    LLVMValueRef fe_is_default = LLVMBuildICmp(ctx->builder, LLVMIntEQ, cur_fe, cap_val, "m.feisdef");
                    LLVMValueRef new_fe = LLVMBuildSelect(ctx->builder, fe_is_default, i_phi, cur_fe, "m.newfe");
                    LLVMBuildStore(ctx->builder, new_fe, fe_alloca);
                    LLVMBuildBr(ctx->builder, next_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
                    LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, i_phi, one64, "m.ni");
                    LLVMBuildBr(ctx->builder, loop_bb);

                    LLVMValueRef i_incoming[] = {zero64, next_i};
                    LLVMBasicBlockRef i_blocks[] = {saved_bb, next_bb};
                    LLVMAddIncoming(i_phi, i_incoming, i_blocks, 2);

                    LLVMPositionBuilderAtEnd(ctx->builder, after_bb);
                    LLVMValueRef fo_res
                        = LLVMBuildLoad2(ctx->builder, LLVMPointerType(val_td->llvm_type, 0), res_alloca, "m.fo");
                    LLVMValueRef fo_isnull = LLVMBuildICmp(
                        ctx->builder,
                        LLVMIntEQ,
                        fo_res,
                        LLVMConstNull(LLVMPointerType(val_td->llvm_type, 0)),
                        "m.fonull"
                    );
                    LLVMValueRef fe_val = LLVMBuildLoad2(ctx->builder, i64t, fe_alloca, "m.fe");
                    LLVMValueRef has_empty = LLVMBuildICmp(ctx->builder, LLVMIntULT, fe_val, cap_val, "m.hasempty");
                    LLVMValueRef need_claim = LLVMBuildAnd(ctx->builder, fo_isnull, has_empty, "m.needclaim");
                    LLVMBuildCondBr(ctx->builder, need_claim, claim_bb, merge_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, claim_bb);
                    LLVMValueRef c_ioff = LLVMBuildMul(ctx->builder, fe_val, stride, "m.ioff2");
                    LLVMValueRef c_entry = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entries_base, &c_ioff, 1, "m.ce");
                    LLVMBuildStore(ctx->builder, LLVMConstInt(i8t, 1, false), c_entry);
                    LLVMValueRef c_kp = LLVMBuildInBoundsGEP2(ctx->builder, i8t, c_entry, &one64, 1, "m.ckp");
                    LLVMValueRef c_kpt
                        = LLVMBuildPointerCast(ctx->builder, c_kp, LLVMPointerType(key_td->llvm_type, 0), "");
                    LLVMBuildStore(ctx->builder, key_to_compare, c_kpt);
                    LLVMValueRef old_cnt = LLVMBuildLoad2(ctx->builder, i64t, cnt_ptr, "m.oldcnt");
                    LLVMBuildStore(ctx->builder, LLVMBuildAdd(ctx->builder, old_cnt, one64, "m.newcnt"), cnt_ptr);
                    LLVMValueRef c_vp = LLVMBuildInBoundsGEP2(ctx->builder, i8t, c_entry, &ks_plus_one, 1, "m.cvp");
                    LLVMValueRef c_vpt
                        = LLVMBuildPointerCast(ctx->builder, c_vp, LLVMPointerType(val_td->llvm_type, 0), "");
                    LLVMBuildStore(ctx->builder, c_vpt, res_alloca);
                    LLVMBuildBr(ctx->builder, merge_bb);

                    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
                    ptr = LLVMBuildLoad2(ctx->builder, LLVMPointerType(val_td->llvm_type, 0), res_alloca, "m.ptr");
                    if (ptr == NULL)
                        return NULL;
                    LLVMValueRef tmp_alloca = LLVMBuildAlloca(ctx->builder, val_td->llvm_type, "map.tmp");
                    LLVMValueRef ptr_null = LLVMBuildICmp(
                        ctx->builder, LLVMIntEQ, ptr, LLVMConstNull(LLVMPointerType(val_td->llvm_type, 0)), "m.ptrnull"
                    );
                    ptr = LLVMBuildSelect(ctx->builder, ptr_null, tmp_alloca, ptr, "m.finalptr");

                    if (val_td)
                        cur_type = val_td;
                }
                else
                {
                    return NULL;
                }
                break;
            }

            case AST_NODE_POSTFIX_MEMBER:
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
                    return NULL;

                if (cur_type == NULL || cur_type->kind != TD_KIND_STRUCT)
                    return NULL;

                field_access_path_t path;
                if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
                    return NULL;

                int n_indices = path.count + 1;
                LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                for (int pi = 0; pi < path.count; pi++)
                {
                    indices[pi + 1]
                        = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
                }
                ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, ptr, indices, (unsigned)n_indices, field_name_node->text
                );

                // Update cur_type to the final field type
                TypeDescriptor const * tmp_type = cur_type;
                for (int pi = 0; pi < path.count; pi++)
                {
                    struct_field_t const * f = type_descriptor_get_struct_field(tmp_type, path.indices[pi]);
                    if (f == NULL)
                        break;
                    if (pi == path.count - 1)
                        cur_type = f->type_desc;
                    else
                        tmp_type = f->type_desc;
                }
                break;
            }

            case AST_NODE_POSTFIX_DEREF:
            {
                if (cur_type == NULL || cur_type->kind != TD_KIND_POINTER)
                    return NULL;
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
    bool is_float = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

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

static LLVMValueRef
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

static LLVMValueRef
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

    LLVMValueRef is_zero;
    LLVMTypeKind tk = LLVMGetTypeKind(lhs_type);
    if (tk == LLVMIntegerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, LLVMConstNull(lhs_type), "orelse_isnil");
    else if (tk == LLVMPointerTypeKind)
        is_zero = LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, LLVMConstNull(lhs_type), "orelse_isnil");
    else
        return lhs;

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "or.rhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "or.merge");

    LLVMBuildCondBr(ctx->builder, is_zero, rhs_bb, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, rhs_bb);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[1]);
    if (rhs == NULL)
        rhs = LLVMConstNull(lhs_type);
    LLVMBuildBr(ctx->builder, merge_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(ctx->builder, lhs_type, "or.phi");
    LLVMValueRef incoming_vals[] = {lhs, rhs};
    LLVMBasicBlockRef incoming_blocks[] = {entry_bb, rhs_bb};
    LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);

    return phi;
}

static LLVMValueRef
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

    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "or.ret");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "or.cont");

    LLVMBuildCondBr(ctx->builder, is_zero, ret_bb, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ret_bb);
    ir_gen_emit_all_defers(ctx);
    if (ctx->current_return_type == type_descriptor_get_void_type(ctx->type_registry))
        LLVMBuildRetVoid(ctx->builder);
    else
        LLVMBuildRet(ctx->builder, LLVMConstNull(ctx->current_return_type->llvm_type));

    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
    return val;
}

static LLVMValueRef
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

    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "tern.then");
    LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "tern.else");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "tern.merge");

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
static void
ir_gen_pack_any(IrGenContext * ctx, LLVMValueRef lhs_ptr, LLVMValueRef rhs_val, LLVMTypeRef any_struct_type)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMValueRef zero_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    LLVMValueRef data_ptr;
    LLVMTypeRef val_type = LLVMTypeOf(rhs_val);
    LLVMTypeKind val_kind = LLVMGetTypeKind(val_type);
    if (val_kind == LLVMIntegerTypeKind)
    {
        data_ptr = LLVMBuildIntToPtr(ctx->builder, rhs_val, i8ptr, "anydata");
    }
    else if (val_kind == LLVMPointerTypeKind)
    {
        data_ptr = LLVMBuildBitCast(ctx->builder, rhs_val, i8ptr, "anydata");
    }
    else
    {
        LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, val_type, "anytmp");
        LLVMBuildStore(ctx->builder, rhs_val, tmp);
        data_ptr = LLVMBuildBitCast(ctx->builder, tmp, i8ptr, "anydata");
    }
    LLVMValueRef type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
    LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    LLVMValueRef gep0[2] = {zero_idx, idx1};
    LLVMValueRef data_field = LLVMBuildInBoundsGEP2(ctx->builder, any_struct_type, lhs_ptr, gep0, 2, "any.data");
    LLVMBuildStore(ctx->builder, data_ptr, data_field);
    LLVMValueRef idx2 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
    LLVMValueRef gep1[2] = {zero_idx, idx2};
    LLVMValueRef id_field = LLVMBuildInBoundsGEP2(ctx->builder, any_struct_type, lhs_ptr, gep1, 2, "any.typeid");
    LLVMBuildStore(ctx->builder, type_id, id_field);
}

static LLVMValueRef
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

    LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, node->list.children[0]);
    if (lhs_ptr == NULL)
        return rhs_val;

    LLVMValueRef store_val = rhs_val;
    TypeDescriptor const * lhs_type_desc = NULL;
    odin_grammar_node_t * lhs_expr = node->list.children[0];
    odin_grammar_node_t * t = lhs_expr;
    while (t && t->list.count >= 1 && t->list.children[0])
        t = t->list.children[0];
    if (t)
        lhs_type_desc = t->resolved_type;
    if (lhs_type_desc && lhs_type_desc->kind == TD_KIND_BASIC && lhs_type_desc->as.basic.name
        && strcmp(lhs_type_desc->as.basic.name, "any") == 0)
    {
        ir_gen_pack_any(ctx, lhs_ptr, rhs_val, lhs_type_desc->llvm_type);
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

static LLVMValueRef
ir_gen_assign_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;

    odin_grammar_node_t * op_node = node_find_op(node);
    AstOpMetadata * op_md = (op_node) ? (AstOpMetadata *)op_node->metadata : NULL;
    OperatorKind op_kind = op_md ? op_md->kind : OP_ASSIGN;

    if (op_kind != OP_ASSIGN && compound_assign_to_binary_op(op_kind) == OP_INVALID)
        return NULL;

    LLVMValueRef rhs_val = ir_gen_node(ctx, node->list.children[2]);
    if (rhs_val == NULL)
        return NULL;

    LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, node->list.children[0]);
    if (lhs_ptr == NULL)
        return rhs_val;

    LLVMValueRef store_val = rhs_val;
    TypeDescriptor const * lhs_type_desc = NULL;
    odin_grammar_node_t * lhs_expr = node->list.children[0];
    odin_grammar_node_t * t = lhs_expr;
    while (t && t->list.count >= 1 && t->list.children[0])
        t = t->list.children[0];
    if (t)
        lhs_type_desc = t->resolved_type;
    if (lhs_type_desc && lhs_type_desc->kind == TD_KIND_BASIC && lhs_type_desc->as.basic.name
        && strcmp(lhs_type_desc->as.basic.name, "any") == 0)
    {
        ir_gen_pack_any(ctx, lhs_ptr, rhs_val, lhs_type_desc->llvm_type);
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

// --- Defer helper functions ---

static void
ir_gen_emit_defers_at_depth(IrGenContext * ctx, int depth)
{
    // Emit defers at given scope depth from top to bottom (LIFO)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        if (ctx->defer_stack[i].scope_depth == depth)
        {
            ir_gen_node(ctx, ctx->defer_stack[i].node);
        }
    }
    // Compact stack, removing entries at this depth
    int write = 0;
    for (int read = 0; read < ctx->defer_count; read++)
    {
        if (ctx->defer_stack[read].scope_depth != depth)
        {
            ctx->defer_stack[write++] = ctx->defer_stack[read];
        }
    }
    ctx->defer_count = write;
}

static void
ir_gen_emit_defers_from_depth(IrGenContext * ctx, int min_depth)
{
    // Emit all defers with scope_depth >= min_depth (inside the loop/switch body)
    // Iterate from top to bottom so inner defers fire first (LIFO)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        if (ctx->defer_stack[i].scope_depth >= min_depth)
        {
            ir_gen_node(ctx, ctx->defer_stack[i].node);
        }
    }
    // Compact stack, keeping only defers with scope_depth < min_depth
    int write = 0;
    for (int read = 0; read < ctx->defer_count; read++)
    {
        if (ctx->defer_stack[read].scope_depth < min_depth)
        {
            ctx->defer_stack[write++] = ctx->defer_stack[read];
        }
    }
    ctx->defer_count = write;
}

static void
ir_gen_emit_all_defers(IrGenContext * ctx)
{
    // Emit all pending defers from top to bottom (LIFO, inner scope first)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        ir_gen_node(ctx, ctx->defer_stack[i].node);
    }
    ctx->defer_count = 0;
}

// --- Statement codegen ---

static LLVMValueRef
ir_gen_return_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    odin_grammar_node_t * expr = node_find_child(node, AST_NODE_EXPRESSION);
    if (expr == NULL)
    {
        // Check for other expression types (bare values without Expression wrapper)
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child != NULL && child->type != AST_NODE_RETURN_STATEMENT)
            {
                expr = child;
                break;
            }
        }
    }

    // Evaluate return value first, then run defers, then ret
    LLVMValueRef val = NULL;
    if (expr != NULL)
    {
        val = ir_gen_node(ctx, expr);
        if (val == NULL)
            return NULL;
    }

    // Emit all pending defers before the return instruction
    ir_gen_emit_all_defers(ctx);

    if (val == NULL)
    {
        return LLVMBuildRetVoid(ctx->builder);
    }
    return LLVMBuildRet(ctx->builder, val);
}

static LLVMValueRef
ir_gen_compound_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    for (size_t i = 0; i < node->list.count; i++)
    {
        ir_gen_node(ctx, node->list.children[i]);
    }
    return NULL;
}

// --- Control flow codegen ---

static LLVMValueRef
ir_gen_if_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * cond_node = node->list.children[0];
    odin_grammar_node_t * then_node = (node->list.count > 1) ? node->list.children[1] : NULL;
    odin_grammar_node_t * else_node = (node->list.count > 2) ? node->list.children[2] : NULL;

    if (cond_node == NULL)
        return NULL;

    LLVMValueRef cond_val = ir_gen_node(ctx, cond_node);
    if (cond_val == NULL)
        return NULL;

    LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
    LLVMTypeKind cond_kind = LLVMGetTypeKind(cond_type);

    LLVMValueRef bool_cond;
    if (cond_kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) == 1)
    {
        bool_cond = cond_val;
    }
    else
    {
        LLVMValueRef zero = LLVMConstNull(cond_type);
        bool_cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "ifcond");
    }

    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "then");
    LLVMBasicBlockRef else_bb
        = else_node ? LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "else") : NULL;
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "ifmerge");

    if (else_bb)
    {
        LLVMBuildCondBr(ctx->builder, bool_cond, then_bb, else_bb);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, bool_cond, then_bb, merge_bb);
    }

    // Then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
    if (then_node)
    {
        ir_gen_node(ctx, then_node);
    }
    LLVMBasicBlockRef then_end = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef then_term = LLVMGetLastInstruction(then_end);
    if (then_term == NULL || !LLVMIsATerminatorInst(then_term))
    {
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    // Else block
    if (else_bb)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
        if (else_node)
        {
            ir_gen_node(ctx, else_node);
        }
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef else_term = LLVMGetLastInstruction(else_end);
        if (else_term == NULL || !LLVMIsATerminatorInst(else_term))
        {
            LLVMBuildBr(ctx->builder, merge_bb);
        }
    }

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    return NULL;
}

static LLVMValueRef
ir_gen_for_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * cond_node = NULL;
    odin_grammar_node_t * body_node = NULL;

    // Detect for-range: first child is a raw Identifier
    bool is_for_range = false;
    odin_grammar_node_t * loop_var_nodes[MAX_LOOP_DEPTH];
    int loop_var_count = 0;
    odin_grammar_node_t * range_expr_node = NULL;

    if (node->list.count >= 2 && node->list.children[0] != NULL && node->list.children[0]->type == AST_NODE_IDENTIFIER)
    {
        for (size_t i = 0; i < node->list.count && loop_var_count < MAX_LOOP_DEPTH; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
                break;
            if (child->type == AST_NODE_IDENTIFIER)
            {
                loop_var_nodes[loop_var_count++] = child;
                continue;
            }
            if (child->resolved_type && child->resolved_type->kind == TD_KIND_RANGE)
            {
                is_for_range = true;
                range_expr_node = child;
            }
            break;
        }
    }

    if (is_for_range)
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMValueRef zero_i32 = LLVMConstInt(i32, 0, false);
        LLVMValueRef one_i32 = LLVMConstInt(i32, 1, false);

        // 1. Evaluate range expression → produces {i64, i64} struct value
        LLVMValueRef range_val = ir_gen_node(ctx, range_expr_node);
        if (!range_val)
        {
            LLVMBuildUnreachable(ctx->builder);
            return NULL;
        }

        // 2. Extract low (idx 0) and high (idx 1) from the struct
        LLVMTypeRef range_struct = LLVMTypeOf(range_val);
        LLVMValueRef range_alloca = LLVMBuildAlloca(ctx->builder, range_struct, "for.range");
        LLVMBuildStore(ctx->builder, range_val, range_alloca);

        LLVMValueRef low_gep_i[2] = {zero_i32, zero_i32};
        LLVMValueRef low_gep
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, low_gep_i, 2, "for.range.low.gep");
        LLVMValueRef low_val = LLVMBuildLoad2(ctx->builder, i64, low_gep, "for.low");

        LLVMValueRef high_gep_i[2] = {zero_i32, one_i32};
        LLVMValueRef high_gep
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, high_gep_i, 2, "for.range.high.gep");
        LLVMValueRef high_val = LLVMBuildLoad2(ctx->builder, i64, high_gep, "for.high");

        // 3. Find body node
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child && child->type == AST_NODE_COMPOUND_STATEMENT)
            {
                body_node = child;
                break;
            }
        }

        // 4. Build loop blocks
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forcond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forbody");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forinc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forend");

        // Push loop context (continue goes to inc block)
        if (ctx->loop_depth < MAX_LOOP_DEPTH)
        {
            ctx->loop_stack[ctx->loop_depth].continue_bb = inc_bb;
            ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
            ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
            ctx->loop_depth++;
        }

        // Allocate loop variables, initialize to low, register in scope
        LLVMValueRef loop_allocas[MAX_LOOP_DEPTH];
        TypeDescriptor const * i64_td = type_descriptor_get_int64_type(ctx->type_registry);
        for (int vi = 0; vi < loop_var_count; vi++)
        {
            char const * name = loop_var_nodes[vi] ? loop_var_nodes[vi]->text : "for.var";
            loop_allocas[vi] = LLVMBuildAlloca(ctx->builder, i64, name);
            LLVMSetAlignment(loop_allocas[vi], LLVMABIAlignmentOfType(ctx->data_layout, i64));
            LLVMBuildStore(ctx->builder, low_val, loop_allocas[vi]);
            TypedValue tv = create_typed_value(loop_allocas[vi], i64_td, true);
            generator_add_symbol(ctx->gen_ctx, name, tv);
        }

        LLVMBuildBr(ctx->builder, cond_bb);

        // Condition block: loop_var < high
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef loop_val = LLVMBuildLoad2(ctx->builder, i64, loop_allocas[0], "for.i.val");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, loop_val, high_val, "for.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        // Body block
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        if (body_node)
        {
            ir_gen_node(ctx, body_node);
        }
        LLVMBasicBlockRef body_end_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef body_term = LLVMGetLastInstruction(body_end_bb);
        if (body_term == NULL || !LLVMIsATerminatorInst(body_term))
        {
            LLVMBuildBr(ctx->builder, inc_bb);
        }

        // Increment block: load first var, increment, store to all vars
        LLVMPositionBuilderAtEnd(ctx->builder, inc_bb);
        LLVMValueRef old_val = LLVMBuildLoad2(ctx->builder, i64, loop_allocas[0], "for.i.old");
        LLVMValueRef inc = LLVMBuildAdd(ctx->builder, old_val, LLVMConstInt(i64, 1, false), "for.i.inc");
        for (int vi = 0; vi < loop_var_count; vi++)
        {
            LLVMBuildStore(ctx->builder, inc, loop_allocas[vi]);
        }
        LLVMBuildBr(ctx->builder, cond_bb);

        // Pop loop context
        if (ctx->loop_depth > 0)
            ctx->loop_depth--;

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    // Original for-loop logic
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_COMPOUND_STATEMENT)
        {
            body_node = child;
        }
        else
        {
            cond_node = child;
        }
    }

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forcond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forbody");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "forend");

    // Push loop context for break/continue
    if (ctx->loop_depth < MAX_LOOP_DEPTH)
    {
        ctx->loop_stack[ctx->loop_depth].continue_bb = cond_bb;
        ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
        ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
        ctx->loop_depth++;
    }

    LLVMBuildBr(ctx->builder, cond_bb);

    // Condition block
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    if (cond_node)
    {
        LLVMValueRef cond_val = ir_gen_node(ctx, cond_node);
        if (cond_val)
        {
            LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
            LLVMTypeKind cond_kind = LLVMGetTypeKind(cond_type);
            LLVMValueRef bool_cond;
            if (cond_kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) == 1)
            {
                bool_cond = cond_val;
            }
            else
            {
                LLVMValueRef zero = LLVMConstNull(cond_type);
                bool_cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "forcond");
            }
            LLVMBuildCondBr(ctx->builder, bool_cond, body_bb, end_bb);
        }
        else
        {
            LLVMBuildBr(ctx->builder, body_bb);
        }
    }
    else
    {
        LLVMBuildBr(ctx->builder, body_bb);
    }

    // Body block
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    if (body_node)
    {
        ir_gen_node(ctx, body_node);
    }
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef body_term = LLVMGetLastInstruction(body_end);
    if (body_term == NULL || !LLVMIsATerminatorInst(body_term))
    {
        LLVMBuildBr(ctx->builder, cond_bb);
    }

    // Pop loop context
    if (ctx->loop_depth > 0)
        ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

static LLVMValueRef
ir_gen_switch_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // SwitchStatement = KwSwitch Directive? Expression? SwitchBody
    // Children: Expression (optional), SwitchCaseClause(s), SwitchDefaultClause
    // (optional)
    odin_grammar_node_t * switch_expr = NULL;
    odin_grammar_node_t * default_case = NULL;
    odin_grammar_node_t * case_clauses[64];
    int case_count = 0;

    // Separate children into expression, cases, and default
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_SWITCH_CASE)
        {
            if (case_count < 64)
                case_clauses[case_count++] = child;
        }
        else if (child->type == AST_NODE_SWITCH_DEFAULT)
        {
            default_case = child;
        }
        else if (child->type == AST_NODE_DIRECTIVE || child->type == AST_NODE_DIRECTIVE_WITH_ARGS)
        {
            // Skip directives
        }
        else
        {
            switch_expr = child;
        }
    }

    if (switch_expr == NULL)
        return NULL;

    LLVMValueRef switch_val = ir_gen_node(ctx, switch_expr);
    if (switch_val == NULL)
        return NULL;

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "swend");

    // Push loop context for break in switch
    if (ctx->loop_depth < MAX_LOOP_DEPTH)
    {
        ctx->loop_stack[ctx->loop_depth].continue_bb = NULL;
        ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
        ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
        ctx->loop_depth++;
    }

    // Create a block for each case + a default block
    LLVMBasicBlockRef case_bbs[64];
    LLVMBasicBlockRef default_bb = NULL;

    for (int i = 0; i < case_count; i++)
    {
        case_bbs[i] = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "case");
    }
    if (default_case)
    {
        default_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "default");
    }

    // Build compare-and-branch chain (like if-else)
    // Each case checks its value and branches to its body; next case on mismatch
    for (int i = 0; i < case_count; i++)
    {
        odin_grammar_node_t * case_clause = case_clauses[i];
        // First child of SwitchCaseClause is KwCase (terminal, no child),
        // then Expression(s) (could be multiple comma-separated values),
        // then Statement*s.
        // The case clause body is embedded in this node (not a separate compound
        // stmt).

        if (case_clause->list.count < 1)
            continue;

        odin_grammar_node_t * case_val_node = NULL;
        // Find the first expression child for the case value
        for (size_t ci = 0; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * child = case_clause->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type != AST_NODE_COMPOUND_STATEMENT && child->type != AST_NODE_RETURN_STATEMENT
                && child->type != AST_NODE_BREAK_STATEMENT && child->type != AST_NODE_CONTINUE_STATEMENT
                && child->type != AST_NODE_EXPRESSION_STATEMENT && child->type != AST_NODE_ASSIGN_STATEMENT
                && child->type != AST_NODE_VARIABLE_DECL && child->type != AST_NODE_IF_STATEMENT
                && child->type != AST_NODE_FOR_STATEMENT && child->type != AST_NODE_SWITCH_STATEMENT)
            {
                case_val_node = child;
                break;
            }
        }

        if (case_val_node == NULL)
            continue;

        LLVMValueRef case_val = ir_gen_node(ctx, case_val_node);
        if (case_val == NULL)
            continue;

        LLVMBasicBlockRef case_bb = case_bbs[i];
        LLVMBasicBlockRef miss_bb;
        if (i < case_count - 1)
            miss_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "swnext");
        else if (default_bb)
            miss_bb = default_bb;
        else
            miss_bb = end_bb;

        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, switch_val, case_val, "swcmp");
        LLVMBuildCondBr(ctx->builder, cmp, case_bb, miss_bb);

        // Case body
        LLVMPositionBuilderAtEnd(ctx->builder, case_bb);

        // Set fallthrough target for any fallthrough statement in this case
        if (i < case_count - 1)
            ctx->fallthrough_target_bb = case_bbs[i + 1];
        else if (default_bb)
            ctx->fallthrough_target_bb = default_bb;
        else
            ctx->fallthrough_target_bb = end_bb;

        ctx->current_scope_depth++;
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 1; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * stmt = case_clause->list.children[ci];
            if (stmt == NULL)
                continue;
            if (stmt == case_val_node)
                continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef case_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef case_term = LLVMGetLastInstruction(case_block_end);
        if (case_term == NULL || !LLVMIsATerminatorInst(case_term))
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
            LLVMBuildBr(ctx->builder, end_bb);
        }
        else
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        }
        ctx->current_scope_depth--;
        generator_pop_scope(ctx->gen_ctx);
        ctx->fallthrough_target_bb = NULL;

        // Position builder at the miss block for next iteration
        if (i < case_count - 1)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, miss_bb);
        }
    }

    // Default case — always position at default_bb
    if (default_bb)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
        ctx->current_scope_depth++;
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 0; ci < default_case->list.count; ci++)
        {
            odin_grammar_node_t * stmt = default_case->list.children[ci];
            if (stmt == NULL)
                continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef def_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef def_term = LLVMGetLastInstruction(def_block_end);
        if (def_term == NULL || !LLVMIsATerminatorInst(def_term))
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
            LLVMBuildBr(ctx->builder, end_bb);
        }
        else
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        }
        ctx->current_scope_depth--;
        generator_pop_scope(ctx->gen_ctx);
    }

    // Pop loop context
    if (ctx->loop_depth > 0)
        ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

// --- Procedure parameter registration ---

static void
ir_gen_register_params(IrGenContext * ctx, odin_grammar_node_t * proc_literal, LLVMValueRef func)
{
    odin_grammar_node_t * sig_node = node_find_child(proc_literal, AST_NODE_PROCEDURE_SIGNATURE);
    if (sig_node == NULL)
        return;

    odin_grammar_node_t * param_list_node = NULL;
    for (size_t i = 0; i < sig_node->list.count; i++)
    {
        odin_grammar_node_t * child = sig_node->list.children[i];
        if (child && child->type == AST_NODE_PARAMETER_LIST)
        {
            param_list_node = child;
            break;
        }
    }
    if (param_list_node == NULL || param_list_node->list.count == 0)
        return;

    odin_grammar_node_t * params = param_list_node->list.children[0];
    if (params == NULL || params->type != AST_NODE_PARAMETERS)
        return;

    unsigned param_index = 0;
    for (size_t k = 0; k < params->list.count; k++)
    {
        odin_grammar_node_t * param = params->list.children[k];
        if (param == NULL || param->type != AST_NODE_PARAMETER)
            continue;

        odin_grammar_node_t * param_ident = NULL;
        odin_grammar_node_t * param_type_node = NULL;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_IDENTIFIER && param_ident == NULL)
                param_ident = child;
            else if (child->type == AST_NODE_IDENTIFIER || is_type_node(child))
                param_type_node = child;
        }
        if (param_type_node == NULL)
        {
            for (size_t ci = param->list.count; ci > 0; ci--)
            {
                odin_grammar_node_t * child = param->list.children[ci - 1];
                if (child == NULL)
                    continue;
                if (child->type == AST_NODE_IDENTIFIER && child != param_ident)
                {
                    param_type_node = child;
                    break;
                }
            }
        }
        if (param_ident == NULL || param_type_node == NULL)
            continue;

        TypeDescriptor const * param_type = param_type_node->resolved_type;
        if (param_type == NULL)
            continue;

        LLVMValueRef param_val = LLVMGetParam(func, param_index);
        LLVMTypeRef llvm_type = param_type->llvm_type;
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, llvm_type, param_ident->text);
        LLVMSetAlignment(alloca, LLVMABIAlignmentOfType(ctx->data_layout, llvm_type));
        LLVMBuildStore(ctx->builder, param_val, alloca);

        TypedValue tv = create_typed_value(alloca, param_type, true);
        generator_add_symbol(ctx->gen_ctx, param_ident->text, tv);

        param_index++;
    }
}

// --- Top-level declaration codegen ---

static LLVMValueRef
ir_gen_top_level_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    odin_grammar_node_t * value_node = node->list.children[1];

    if (name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    if (value_node->type == AST_NODE_PROCEDURE_LITERAL)
    {
        TypeDescriptor const * proc_type = value_node->resolved_type;
        if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
            return NULL;

        LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, proc_type->proc_metadata.func_type);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        ctx->current_function = func;
        ctx->current_return_type = proc_type->proc_metadata.return_type;

        odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);

        generator_push_scope(ctx->gen_ctx);
        ir_gen_register_params(ctx, value_node, func);

        if (body_node)
        {
            ir_gen_node(ctx, body_node);
        }

        LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
        if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
            if (ctx->current_return_type == type_descriptor_get_void_type(ctx->type_registry))
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                LLVMBuildRet(ctx->builder, LLVMConstNull(ctx->current_return_type->llvm_type));
            }
        }

        generator_pop_scope(ctx->gen_ctx);

        TypedValue tv = create_typed_value(func, proc_type, false);
        generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

        ctx->current_function = NULL;
        ctx->current_return_type = NULL;

        return func;
    }

    return NULL;
}

static LLVMValueRef
ir_gen_top_level_variable(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    TypeDescriptor const * var_type = node->resolved_type;
    if (var_type == NULL && node->list.count >= 2 && node->list.children[1])
    {
        var_type = node->list.children[1]->resolved_type;
    }
    if (var_type == NULL)
    {
        var_type = type_descriptor_get_int64_type(ctx->type_registry);
    }

    LLVMTypeRef llvm_type = var_type->llvm_type;
    LLVMValueRef global = LLVMAddGlobal(ctx->module, llvm_type, name_node->text);

    // Initialize if value present
    if (node->list.count >= 3)
    {
        odin_grammar_node_t * init_node = node->list.children[2];
        if (init_node)
        {
            LLVMValueRef init_val = ir_gen_node(ctx, init_node);
            if (init_val)
            {
                LLVMSetInitializer(global, init_val);
            }
        }
    }
    else if (node->list.count == 2)
    {
        odin_grammar_node_t * second = node->list.children[1];
        if (second && !is_type_node(second))
        {
            LLVMValueRef init_val = ir_gen_node(ctx, second);
            if (init_val)
            {
                LLVMSetInitializer(global, init_val);
            }
        }
    }

    TypedValue tv = create_typed_value(global, var_type, true);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

    return global;
}

// --- Nested procedure declaration codegen ---

static LLVMValueRef
ir_gen_nested_procedure_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;
    odin_grammar_node_t * name_node = node->list.children[0];
    odin_grammar_node_t * value_node = node->list.children[1];
    if (name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    if (value_node->type != AST_NODE_PROCEDURE_LITERAL)
        return NULL;

    TypeDescriptor const * proc_type = value_node->resolved_type;
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
        return NULL;

    // Save outer function context
    LLVMValueRef outer_func = ctx->current_function;
    TypeDescriptor const * outer_ret_type = ctx->current_return_type;
    LLVMBasicBlockRef outer_block = LLVMGetInsertBlock(ctx->builder);

    // Build nested function
    LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, proc_type->proc_metadata.func_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    ctx->current_function = func;
    ctx->current_return_type = proc_type->proc_metadata.return_type;

    generator_push_scope(ctx->gen_ctx);
    ir_gen_register_params(ctx, value_node, func);

    odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);
    if (body_node)
    {
        ir_gen_node(ctx, body_node);
    }

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
    if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
    {
        ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        if (ctx->current_return_type == type_descriptor_get_void_type(ctx->type_registry))
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            LLVMBuildRet(ctx->builder, LLVMConstNull(ctx->current_return_type->llvm_type));
        }
    }

    generator_pop_scope(ctx->gen_ctx);

    // Register in current (enclosing) scope
    TypedValue tv = create_typed_value(func, proc_type, false);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

    // Restore outer function context
    ctx->current_function = outer_func;
    ctx->current_return_type = outer_ret_type;
    if (outer_block != NULL)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, outer_block);
    }

    return func;
}

// --- Postfix expression / call codegen ---

// Walk a comma-chained Expression tree to collect individual argument values.
// Comma is a terminal (lexeme) so it produces no AST node.
// chainl1(AssignExpression, Comma) produces a left-associative tree:
//   Expr(Expr(a, b), c)   for a, b, c
static int
ir_gen_collect_call_args(IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef * args, int max_args)
{
    if (node == NULL || max_args <= 0)
        return 0;

    // Detect comma-chainl1: AST_NODE_EXPRESSION with >=2 children
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
        // children[0] is the left sub-chain, children[last] is the rightmost
        // operand
        odin_grammar_node_t * last = node->list.children[node->list.count - 1];
        int count = ir_gen_collect_call_args(ctx, node->list.children[0], args, max_args);
        if (count < max_args && last != NULL)
        {
            args[count] = ir_gen_node(ctx, last);
            count++;
        }
        return count;
    }

    // Single expression — evaluate directly
    args[0] = ir_gen_node(ctx, node);
    return args[0] ? 1 : 0;
}

static LLVMValueRef
ir_gen_postfix_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    LLVMValueRef val = ir_gen_node(ctx, node->list.children[0]);

    if (node->list.count < 2)
        return val;
    odin_grammar_node_t * postfix_ops = node->list.children[1];
    if (postfix_ops == NULL || postfix_ops->type != AST_NODE_POSTFIX_OPS)
        return val;

    TypeDescriptor const * cur_type = node->list.children[0] ? node->list.children[0]->resolved_type : NULL;

    for (size_t i = 0; i < postfix_ops->list.count; i++)
    {
        odin_grammar_node_t * op = postfix_ops->list.children[i];
        if (op == NULL)
            continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

        switch (op->type)
        {
        case AST_NODE_POSTFIX_CALL:
        {
            odin_grammar_node_t * ident = expression_unwrap_to_identifier(node->list.children[0]);
            TypeDescriptor const * proc_type = NULL;
            if (ident)
            {
                symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), ident->text);
                if (sym)
                    proc_type = sym->value.type_info;
            }
            if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
            {
                if (cur_type && cur_type->kind == TD_KIND_PROC)
                    proc_type = cur_type;
            }
            if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
                return val;

            LLVMTypeRef func_type = proc_type->proc_metadata.func_type;

            LLVMValueRef args[128];
            int arg_count = 0;

            if (op->list.count > 0 && op->list.children[0] != NULL)
            {
                odin_grammar_node_t * arg_expr = op->list.children[0];
                if (arg_expr->type == AST_NODE_ARGUMENT_LIST && arg_expr->list.count > 0)
                    arg_expr = arg_expr->list.children[0];
                arg_count = ir_gen_collect_call_args(ctx, arg_expr, args, 128);
            }

            val = LLVMBuildCall2(ctx->builder, func_type, val, args, (unsigned)arg_count, "calltmp");

            if (proc_type->proc_metadata.return_type)
                cur_type = proc_type->proc_metadata.return_type;
            break;
        }

        case AST_NODE_POSTFIX_SUBSCRIPT:
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
                break;

            LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
            if (index_val == NULL)
                break;

            if (cur_type == NULL)
                break;

            if (cur_type->kind == TD_KIND_ARRAY)
            {
                LLVMTypeRef arr_type = cur_type->llvm_type;
                LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false), index_val};
                LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(ctx->builder, arr_type, val, indices, 2, "subs");
                val = elem_ptr;
            }
            else if (cur_type->kind == TD_KIND_SLICE || cur_type->kind == TD_KIND_DYNAMIC_ARRAY)
            {
                LLVMValueRef data_indices[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                LLVMValueRef data_field
                    = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, val, data_indices, 2, "slice.data.ptr");
                LLVMValueRef data = LLVMBuildLoad2(
                    ctx->builder, LLVMPointerType(cur_type->element_type->llvm_type, 0), data_field, "slice.data"
                );

                val = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->element_type->llvm_type, data, &index_val, 1, "slice.subs"
                );
            }
            else if (cur_type->kind == TD_KIND_MAP)
            {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
                LLVMTypeRef i8t = LLVMInt8TypeInContext(ctx->context);
                LLVMValueRef zero64 = LLVMConstInt(i64t, 0, false);
                LLVMValueRef one64 = LLVMConstInt(i64t, 1, false);

                TypeDescriptor const * key_td = cur_type->as.map.key_type;
                TypeDescriptor const * val_td = cur_type->as.map.value_type;

                LLVMValueRef data_ptr = NULL;
                LLVMTypeRef val_llvm_type = LLVMTypeOf(val);
                if (LLVMGetTypeKind(val_llvm_type) == LLVMPointerTypeKind)
                {
                    LLVMValueRef didx[]
                        = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                           LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                    LLVMValueRef dgep
                        = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, val, didx, 2, "map.r.dgep");
                    data_ptr = LLVMBuildLoad2(ctx->builder, LLVMPointerType(i8t, 0), dgep, "map.r.data");
                }
                else
                {
                    data_ptr = LLVMBuildExtractValue(ctx->builder, val, 0, "map.r.data");
                }
                if (data_ptr == NULL)
                    break;

                LLVMValueRef cap_off = LLVMConstInt(i64t, 8, false);
                LLVMValueRef cap_ptr = LLVMBuildPointerCast(
                    ctx->builder,
                    LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &cap_off, 1, ""),
                    LLVMPointerType(i64t, 0),
                    ""
                );
                LLVMValueRef cap_val = LLVMBuildLoad2(ctx->builder, i64t, cap_ptr, "map.r.cap");

                LLVMValueRef ks_off = LLVMConstInt(i64t, 16, false);
                LLVMValueRef ks_ptr = LLVMBuildPointerCast(
                    ctx->builder,
                    LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &ks_off, 1, ""),
                    LLVMPointerType(i64t, 0),
                    ""
                );
                LLVMValueRef key_sz = LLVMBuildLoad2(ctx->builder, i64t, ks_ptr, "map.r.ks");

                LLVMValueRef vs_off = LLVMConstInt(i64t, 24, false);
                LLVMValueRef vs_ptr = LLVMBuildPointerCast(
                    ctx->builder,
                    LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &vs_off, 1, ""),
                    LLVMPointerType(i64t, 0),
                    ""
                );
                LLVMValueRef val_sz = LLVMBuildLoad2(ctx->builder, i64t, vs_ptr, "map.r.vs");

                LLVMValueRef hdr32 = LLVMConstInt(i64t, 32, false);
                LLVMValueRef entries_base = LLVMBuildInBoundsGEP2(ctx->builder, i8t, data_ptr, &hdr32, 1, "map.r.ents");

                LLVMValueRef stride = LLVMBuildAdd(
                    ctx->builder, one64, LLVMBuildAdd(ctx->builder, key_sz, val_sz, "rkvsz"), "map.r.stride"
                );
                LLVMValueRef ks_plus_one = LLVMBuildAdd(ctx->builder, key_sz, one64, "map.r.ksp1");

                LLVMValueRef key_to_compare
                    = LLVMBuildIntCast(ctx->builder, index_val, key_td->llvm_type, "map.r.key.cast");

                LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(ctx->builder);
                LLVMBasicBlockRef loop_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.loop");
                LLVMBasicBlockRef body_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.body");
                LLVMBasicBlockRef kchk_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.kchk");
                LLVMBasicBlockRef found_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.found");
                LLVMBasicBlockRef next_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.next");
                LLVMBasicBlockRef notfound_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.notfound");
                LLVMBasicBlockRef merge_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "mr.merge");

                LLVMBuildBr(ctx->builder, loop_bb);
                LLVMPositionBuilderAtEnd(ctx->builder, loop_bb);
                LLVMValueRef i_phi = LLVMBuildPhi(ctx->builder, i64t, "mr.i");
                LLVMValueRef loop_cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, i_phi, cap_val, "mr.lcmp");
                LLVMBuildCondBr(ctx->builder, loop_cmp, body_bb, notfound_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
                LLVMValueRef ioff = LLVMBuildMul(ctx->builder, i_phi, stride, "mr.ioff");
                LLVMValueRef entry_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entries_base, &ioff, 1, "mr.ep");
                LLVMValueRef occupied = LLVMBuildLoad2(ctx->builder, i8t, entry_ptr, "mr.occ");
                LLVMValueRef occ_cmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, occupied, LLVMConstNull(i8t), "mr.occmp");
                LLVMBuildCondBr(ctx->builder, occ_cmp, kchk_bb, next_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, kchk_bb);
                LLVMValueRef key_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &one64, 1, "mr.kp");
                LLVMValueRef kp_typed
                    = LLVMBuildPointerCast(ctx->builder, key_ptr, LLVMPointerType(key_td->llvm_type, 0), "");
                LLVMValueRef loaded_key = LLVMBuildLoad2(ctx->builder, key_td->llvm_type, kp_typed, "mr.lk");
                LLVMValueRef key_eq = LLVMBuildICmp(ctx->builder, LLVMIntEQ, key_to_compare, loaded_key, "mr.keq");
                LLVMBuildCondBr(ctx->builder, key_eq, found_bb, next_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, found_bb);
                LLVMValueRef val_ptr = LLVMBuildInBoundsGEP2(ctx->builder, i8t, entry_ptr, &ks_plus_one, 1, "mr.vp");
                LLVMValueRef vp_typed
                    = LLVMBuildPointerCast(ctx->builder, val_ptr, LLVMPointerType(val_td->llvm_type, 0), "");
                LLVMValueRef loaded_val = LLVMBuildLoad2(ctx->builder, val_td->llvm_type, vp_typed, "mr.val");
                LLVMBuildBr(ctx->builder, merge_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, next_bb);
                LLVMValueRef next_i = LLVMBuildAdd(ctx->builder, i_phi, one64, "mr.ni");
                LLVMBuildBr(ctx->builder, loop_bb);

                LLVMValueRef i_incoming[] = {zero64, next_i};
                LLVMBasicBlockRef i_blocks[] = {saved_bb, next_bb};
                LLVMAddIncoming(i_phi, i_incoming, i_blocks, 2);

                LLVMPositionBuilderAtEnd(ctx->builder, notfound_bb);
                LLVMValueRef zero_val = LLVMConstNull(val_td->llvm_type);
                LLVMBuildBr(ctx->builder, merge_bb);

                LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
                LLVMValueRef result_phi = LLVMBuildPhi(ctx->builder, val_td->llvm_type, "mr.res");
                LLVMValueRef phi_vals[] = {loaded_val, zero_val};
                LLVMBasicBlockRef phi_blocks[] = {found_bb, notfound_bb};
                LLVMAddIncoming(result_phi, phi_vals, phi_blocks, 2);
                val = result_phi;
            }
            else
            {
                break;
            }

            if (cur_type->kind == TD_KIND_MAP && op->resolved_type)
                cur_type = op->resolved_type;
            else if (cur_type->element_type)
                cur_type = cur_type->element_type;
            break;
        }

        case AST_NODE_POSTFIX_MEMBER:
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
                break;

            if (cur_type == NULL || cur_type->kind != TD_KIND_STRUCT)
                break;

            field_access_path_t path;
            if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
                break;

            int n_indices = path.count + 1;
            LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            for (int pi = 0; pi < path.count; pi++)
            {
                indices[pi + 1] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
            }

            LLVMValueRef field_ptr = LLVMBuildInBoundsGEP2(
                ctx->builder, cur_type->llvm_type, val, indices, (unsigned)n_indices, field_name_node->text
            );
            val = field_ptr;

            // Update cur_type to the final field type
            TypeDescriptor const * tmp_type = cur_type;
            for (int pi = 0; pi < path.count; pi++)
            {
                struct_field_t const * f = type_descriptor_get_struct_field(tmp_type, path.indices[pi]);
                if (f == NULL)
                    break;
                if (pi == path.count - 1)
                    cur_type = f->type_desc;
                else
                    tmp_type = f->type_desc;
            }
            break;
        }

        case AST_NODE_POSTFIX_DEREF:
        {
            if (cur_type == NULL || cur_type->kind != TD_KIND_POINTER)
                break;
            TypeDescriptor const * pointee_type = cur_type->pointee;
            if (pointee_type == NULL)
                break;
            val = LLVMBuildLoad2(ctx->builder, pointee_type->llvm_type, val, "deref");
            cur_type = pointee_type;
            break;
        }

        case AST_NODE_POSTFIX_ASSERTION:
        {
            // Type assertion x.(T) for 'any': extract data pointer, bitcast, load
            if (cur_type == NULL || cur_type->kind != TD_KIND_BASIC || cur_type->as.basic.name == NULL
                || strcmp(cur_type->as.basic.name, "any") != 0)
                break;
            TypeDescriptor const * target_type = op->resolved_type;
            if (target_type == NULL)
                break;
            // Use alloca+store+GEP+Load to extract field 0 (data pointer) from the any struct
            LLVMValueRef tmp_alloca = LLVMBuildAlloca(ctx->builder, cur_type->llvm_type, "assert.tmp");
            LLVMBuildStore(ctx->builder, val, tmp_alloca);
            LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            LLVMValueRef gep_data[2] = {idx0, idx0};
            LLVMValueRef data_field
                = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, tmp_alloca, gep_data, 2, "assert.data.ptr");
            LLVMValueRef data_ptr = LLVMBuildLoad2(
                ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_field, "assert.data"
            );
            // Extract according to target type:
            // - integer: data was packed via inttoptr, so ptrtoint back
            // - pointer: data was packed via bitcast, so bitcast back
            // - struct/array: data was packed via alloca+store+bitcast, so load through
            if (target_type->kind == TD_KIND_BASIC && !target_type->as.basic.is_float && target_type->as.basic.width > 0
                && target_type->as.basic.width <= 64)
            {
                val = LLVMBuildPtrToInt(ctx->builder, data_ptr, target_type->llvm_type, "assert.val");
            }
            else if (target_type->kind == TD_KIND_POINTER)
            {
                val = LLVMBuildBitCast(ctx->builder, data_ptr, target_type->llvm_type, "assert.val");
            }
            else
            {
                LLVMValueRef typed_ptr = LLVMBuildBitCast(
                    ctx->builder, data_ptr, LLVMPointerType(target_type->llvm_type, 0), "assert.typed"
                );
                val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "assert.val");
            }
            cur_type = target_type;
            break;
        }

        case AST_NODE_POSTFIX_SLICE:
        case AST_NODE_POSTFIX_SLICE_LT:
        {
            if (cur_type == NULL || (cur_type->kind != TD_KIND_SLICE && cur_type->kind != TD_KIND_ARRAY))
                break;

            TypeDescriptor const * slice_type = cur_type;
            TypeDescriptor const * elem_type = slice_type->element_type;
            if (elem_type == NULL)
                break;

            LLVMValueRef data, len;

            if (cur_type->kind == TD_KIND_SLICE)
            {
                // Load data pointer from slice struct field 0 via GEP+Load
                LLVMValueRef field0_indices[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                LLVMValueRef data_gep = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, val, field0_indices, 2, "slice.data.gep"
                );
                data = LLVMBuildLoad2(ctx->builder, LLVMPointerType(elem_type->llvm_type, 0), data_gep, "slice.data");
                // Load length from slice struct field 1 via GEP+Load
                LLVMValueRef field1_indices[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
                LLVMValueRef len_gep
                    = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, val, field1_indices, 2, "slice.len.gep");
                len = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), len_gep, "slice.len");
            }
            else
            {
                // For arrays, create a slice view: {ptr_to_first_elem, len}
                slice_type = get_or_create_slice_type(ctx->type_registry, elem_type);
                LLVMValueRef zero_indices[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                data = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, val, zero_indices, 2, "arr.ptr");
                len = LLVMConstInt(
                    LLVMInt64TypeInContext(ctx->context), (unsigned long long)cur_type->as.array.count, false
                );
            }

            // Determine which bounds are present
            slice_bounds_info bounds = slice_get_bounds_info(op);

            // Compute new data and len
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

            // Build new slice struct
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
            val = LLVMBuildLoad2(ctx->builder, slice_type->llvm_type, slice_ptr, "slice.res");
            cur_type = slice_type;
            break;
        }

        default:
            break;
        }

#pragma GCC diagnostic pop
    }

    // If the final value is a pointer and the result type is non-composite, load
    // it. Proc types are excluded because the pointer IS the value (function
    // pointer).
    if (val != NULL && cur_type != NULL)
    {
        LLVMTypeRef val_llvm_type = LLVMTypeOf(val);
        if (LLVMGetTypeKind(val_llvm_type) == LLVMPointerTypeKind)
        {
            if (cur_type->kind != TD_KIND_STRUCT && cur_type->kind != TD_KIND_ARRAY && cur_type->kind != TD_KIND_SLICE
                && cur_type->kind != TD_KIND_PROC && cur_type->kind != TD_KIND_DYNAMIC_ARRAY
                && cur_type->kind != TD_KIND_MAP)
            {
                val = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "loadtmp");
            }
        }
    }

    return val;
}

// --- Heap allocation helpers ---

static LLVMValueRef
ir_gen_call_malloc(IrGenContext * ctx, LLVMValueRef size)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef malloc_args[] = {LLVMInt64TypeInContext(ctx->context)};
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, false);
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    if (malloc_fn == NULL)
        malloc_fn = LLVMAddFunction(ctx->module, "malloc", malloc_type);
    LLVMValueRef args[] = {size};
    return LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn, args, 1, "malloc");
}

static void
ir_gen_call_free(IrGenContext * ctx, LLVMValueRef ptr)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef free_args[] = {i8ptr};
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), free_args, 1, false);
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    if (free_fn == NULL)
        free_fn = LLVMAddFunction(ctx->module, "free", free_type);
    LLVMValueRef args[] = {LLVMBuildPointerCast(ctx->builder, ptr, i8ptr, "")};
    LLVMBuildCall2(ctx->builder, free_type, free_fn, args, 1, "");
}

static LLVMValueRef
ir_gen_call_calloc(IrGenContext * ctx, LLVMValueRef size)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef calloc_args[] = {LLVMInt64TypeInContext(ctx->context), LLVMInt64TypeInContext(ctx->context)};
    LLVMTypeRef calloc_type = LLVMFunctionType(i8ptr, calloc_args, 2, false);
    LLVMValueRef calloc_fn = LLVMGetNamedFunction(ctx->module, "calloc");
    if (calloc_fn == NULL)
        calloc_fn = LLVMAddFunction(ctx->module, "calloc", calloc_type);
    LLVMValueRef one_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false);
    LLVMValueRef args[] = {one_val, size};
    return LLVMBuildCall2(ctx->builder, calloc_type, calloc_fn, args, 2, "calloc");
}

// --- Main node dispatcher ---

static LLVMValueRef
ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (node->type)
    {
    case AST_NODE_INTEGER_VALUE:
        return ir_gen_integer_value(ctx, node);

    case AST_NODE_FLOAT_VALUE:
        return ir_gen_float_value(ctx, node);

    case AST_NODE_STRING_LITERAL:
    case AST_NODE_RAW_STRING_LITERAL:
        return ir_gen_string_literal(ctx, node);

    case AST_NODE_RUNE_LITERAL:
        return ir_gen_rune_literal(ctx, node);

    case AST_NODE_BOOL_TRUE:
    case AST_NODE_BOOL_FALSE:
        return ir_gen_bool_value(ctx, node);

    case AST_NODE_CAST_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        odin_grammar_node_t * expr_node = node->list.children[1];
        LLVMValueRef src_val = ir_gen_node(ctx, expr_node);
        if (src_val == NULL)
            return NULL;

        TypeDescriptor const * dest_type = node->resolved_type;
        if (dest_type == NULL)
            return NULL;

        LLVMTypeRef dest_llvm_type = dest_type->llvm_type;
        LLVMTypeRef src_llvm_type = LLVMTypeOf(src_val);
        LLVMTypeKind src_kind = LLVMGetTypeKind(src_llvm_type);
        LLVMTypeKind dest_kind = LLVMGetTypeKind(dest_llvm_type);

        if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMIntegerTypeKind)
        {
            unsigned src_w = LLVMGetIntTypeWidth(src_llvm_type);
            unsigned dst_w = LLVMGetIntTypeWidth(dest_llvm_type);
            if (dst_w > src_w)
                return LLVMBuildIntCast2(ctx->builder, src_val, dest_llvm_type, false, "zext");
            else
                return LLVMBuildIntCast2(ctx->builder, src_val, dest_llvm_type, false, "trunc");
        }
        else if ((src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind)
                 && (dest_kind == LLVMFloatTypeKind || dest_kind == LLVMDoubleTypeKind))
        {
            return LLVMBuildFPCast(ctx->builder, src_val, dest_llvm_type, "fpcast");
        }
        else if ((src_kind == LLVMIntegerTypeKind)
                 && (dest_kind == LLVMFloatTypeKind || dest_kind == LLVMDoubleTypeKind))
        {
            return LLVMBuildSIToFP(ctx->builder, src_val, dest_llvm_type, "sitofp");
        }
        else if ((src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind) && dest_kind == LLVMIntegerTypeKind)
        {
            return LLVMBuildFPToSI(ctx->builder, src_val, dest_llvm_type, "fptosi");
        }
        else if (src_kind == LLVMPointerTypeKind && dest_kind == LLVMPointerTypeKind)
        {
            return LLVMBuildPointerCast(ctx->builder, src_val, dest_llvm_type, "ptrcast");
        }
        else if (src_kind == LLVMPointerTypeKind && dest_kind == LLVMIntegerTypeKind)
        {
            return LLVMBuildPtrToInt(ctx->builder, src_val, dest_llvm_type, "ptrint");
        }
        else if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMPointerTypeKind)
        {
            return LLVMBuildIntToPtr(ctx->builder, src_val, dest_llvm_type, "intptr");
        }
        // Fallback for same-size casts
        return LLVMBuildBitCast(ctx->builder, src_val, dest_llvm_type, "cast");
    }

    case AST_NODE_TRANSMUTE_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        odin_grammar_node_t * expr_node = node->list.children[1];
        LLVMValueRef src_val = ir_gen_node(ctx, expr_node);
        if (src_val == NULL)
            return NULL;

        TypeDescriptor const * dest_type = node->resolved_type;
        if (dest_type == NULL)
            return NULL;

        LLVMTypeRef dest_llvm_type = dest_type->llvm_type;
        return LLVMBuildBitCast(ctx->builder, src_val, dest_llvm_type, "transmute");
    }

    case AST_NODE_LEN_EXPR:
    case AST_NODE_CAP_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand = node->list.children[0];
        LLVMValueRef operand_val = ir_gen_node(ctx, operand);
        if (operand_val == NULL)
            return NULL;

        TypeDescriptor const * operand_type = operand->resolved_type;
        if (operand_type == NULL)
            return NULL;

        // Array: compile-time constant from type descriptor
        if (operand_type->kind == TD_KIND_ARRAY)
        {
            return LLVMConstInt(
                LLVMInt64TypeInContext(ctx->context), (unsigned long long)operand_type->as.array.count, false
            );
        }

        // Map: extract data pointer, load count (offset 0) or capacity (offset 8)
        if (operand_type->kind == TD_KIND_MAP)
        {
            bool is_cap_map = (node->type == AST_NODE_CAP_EXPR);
            LLVMValueRef data_ptr_map = NULL;
            LLVMTypeRef val_type_map = LLVMTypeOf(operand_val);
            if (LLVMGetTypeKind(val_type_map) == LLVMPointerTypeKind)
            {
                LLVMValueRef zz[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                data_ptr_map = LLVMBuildLoad2(
                    ctx->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0),
                    LLVMBuildInBoundsGEP2(ctx->builder, operand_type->llvm_type, operand_val, zz, 2, "map.ptr.gep"),
                    ""
                );
            }
            else
            {
                data_ptr_map = LLVMBuildExtractValue(ctx->builder, operand_val, 0, "map.ptr");
            }
            if (data_ptr_map == NULL)
                return NULL;
            LLVMValueRef off_map = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), is_cap_map ? 8 : 0, false);
            LLVMValueRef cnt_ptr_map = LLVMBuildPointerCast(
                ctx->builder,
                LLVMBuildInBoundsGEP2(
                    ctx->builder, LLVMInt8TypeInContext(ctx->context), data_ptr_map, &off_map, 1, "map.cnt.gep"
                ),
                LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
                ""
            );
            LLVMValueRef len_val_map = LLVMBuildLoad2(
                ctx->builder, LLVMInt64TypeInContext(ctx->context), cnt_ptr_map, is_cap_map ? "map.cap" : "map.len"
            );
            if (len_val_map == NULL)
                return NULL;
            return len_val_map;
        }

        // For dynamic arrays: len = field 1, cap = field 2
        // For slices/strings: both len and cap use field 1
        bool is_cap = (node->type == AST_NODE_CAP_EXPR);
        int field_index = (is_cap && operand_type->kind == TD_KIND_DYNAMIC_ARRAY) ? 2 : 1;
        char const * field_name = (field_index == 2) ? "cap" : "len";

        LLVMTypeRef val_type = LLVMTypeOf(operand_val);
        LLVMTypeKind val_kind = LLVMGetTypeKind(val_type);
        LLVMValueRef len_val = NULL;

        if (val_kind == LLVMPointerTypeKind)
        {
            LLVMTypeRef struct_type = operand_type->llvm_type;
            LLVMValueRef indices[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_index, false)};
            LLVMValueRef field_ptr
                = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, operand_val, indices, 2, "len.ptr");
            len_val = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), field_ptr, field_name);
        }
        else
        {
            len_val = LLVMBuildExtractValue(ctx->builder, operand_val, (unsigned)field_index, field_name);
        }

        if (len_val == NULL)
            return NULL;
        return len_val;
    }

    case AST_NODE_MAKE_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        odin_grammar_node_t * len_node = node->list.children[1];

        TypeDescriptor const * result_type = node->resolved_type;
        if (result_type == NULL
            || (result_type->kind != TD_KIND_SLICE && result_type->kind != TD_KIND_DYNAMIC_ARRAY
                && result_type->kind != TD_KIND_MAP))
            return NULL;

        LLVMValueRef len_val = ir_gen_node(ctx, len_node);
        if (len_val == NULL)
            return NULL;
        len_val = LLVMBuildIntCast(ctx->builder, len_val, LLVMInt64TypeInContext(ctx->context), "len.i64");

        LLVMValueRef make_ptr = LLVMBuildAlloca(ctx->builder, result_type->llvm_type, "make.result");

        if (result_type->kind == TD_KIND_MAP)
        {
            TypeDescriptor const * key_type = result_type->as.map.key_type;
            TypeDescriptor const * val_type = result_type->as.map.value_type;
            if (key_type == NULL || val_type == NULL)
                return NULL;

            LLVMValueRef key_size = LLVMSizeOf(key_type->llvm_type);
            LLVMValueRef val_size = LLVMSizeOf(val_type->llvm_type);

            LLVMValueRef one_i64 = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false);
            LLVMValueRef entry_size = LLVMBuildAdd(
                ctx->builder, one_i64, LLVMBuildAdd(ctx->builder, key_size, val_size, "kv.size"), "entry.size"
            );

            LLVMValueRef entries_size = LLVMBuildMul(ctx->builder, len_val, entry_size, "entries.size");
            LLVMValueRef hdr32 = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 32, false);
            LLVMValueRef total_size = LLVMBuildAdd(ctx->builder, hdr32, entries_size, "map.total.size");

            LLVMValueRef map_data = ir_gen_call_calloc(ctx, total_size);
            if (map_data == NULL)
                return NULL;

            LLVMTypeRef i8t = LLVMInt8TypeInContext(ctx->context);

            // Store capacity at offset 8
            LLVMValueRef cidx = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 8, false);
            LLVMValueRef cptr = LLVMBuildPointerCast(
                ctx->builder,
                LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &cidx, 1, ""),
                LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
                ""
            );
            LLVMBuildStore(ctx->builder, len_val, cptr);

            // Store key_size at offset 16
            LLVMValueRef ksid = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 16, false);
            LLVMValueRef ksptr = LLVMBuildPointerCast(
                ctx->builder,
                LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &ksid, 1, ""),
                LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
                ""
            );
            LLVMBuildStore(ctx->builder, key_size, ksptr);

            // Store value_size at offset 24
            LLVMValueRef vsid = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 24, false);
            LLVMValueRef vsptr = LLVMBuildPointerCast(
                ctx->builder,
                LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &vsid, 1, ""),
                LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
                ""
            );
            LLVMBuildStore(ctx->builder, val_size, vsptr);

            // Store map_data pointer into field 0 of result struct
            LLVMValueRef didx[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
            LLVMBuildStore(
                ctx->builder,
                map_data,
                LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, didx, 2, "make.map.data.gep")
            );
        }
        else
        {
            TypeDescriptor const * elem_type = result_type->element_type;
            if (elem_type == NULL)
                return NULL;

            bool is_da = (result_type->kind == TD_KIND_DYNAMIC_ARRAY);

            LLVMValueRef elem_size = LLVMSizeOf(elem_type->llvm_type);
            LLVMValueRef total_size = LLVMBuildMul(ctx->builder, elem_size, len_val, "makemem.size");

            LLVMValueRef raw_mem = ir_gen_call_malloc(ctx, total_size);
            if (raw_mem == NULL)
                return NULL;
            LLVMTypeRef elem_ptr_type = LLVMPointerType(elem_type->llvm_type, 0);
            LLVMValueRef data_ptr = LLVMBuildPointerCast(ctx->builder, raw_mem, elem_ptr_type, "make.data");

            // Store data pointer (field 0)
            LLVMValueRef didx[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
            LLVMBuildStore(
                ctx->builder,
                data_ptr,
                LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, didx, 2, "make.data.gep")
            );

            // Store len (field 1)
            LLVMValueRef lidx[]
                = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                   LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
            LLVMBuildStore(
                ctx->builder,
                len_val,
                LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, lidx, 2, "make.len.gep")
            );

            // For dynamic arrays, also store capacity = len (field 2)
            if (is_da)
            {
                LLVMValueRef cidx[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 2, false)};
                LLVMBuildStore(
                    ctx->builder,
                    len_val,
                    LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, cidx, 2, "make.cap.gep")
                );
            }
        }

        return LLVMBuildLoad2(ctx->builder, result_type->llvm_type, make_ptr, "make.result");
    }

    case AST_NODE_NEW_EXPR:
    {
        if (node->list.count < 1)
            return NULL;

        TypeDescriptor const * ptr_type = node->resolved_type;
        if (ptr_type == NULL || ptr_type->kind != TD_KIND_POINTER)
            return NULL;
        TypeDescriptor const * pointee_type = ptr_type->pointee;
        if (pointee_type == NULL)
            return NULL;

        LLVMValueRef size = LLVMSizeOf(pointee_type->llvm_type);
        LLVMValueRef raw_mem = ir_gen_call_malloc(ctx, size);
        if (raw_mem == NULL)
            return NULL;
        return LLVMBuildPointerCast(ctx->builder, raw_mem, ptr_type->llvm_type, "new.result");
    }

    case AST_NODE_DELETE_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * target = node->list.children[0];
        LLVMValueRef ptr_val = ir_gen_node(ctx, target);
        if (ptr_val == NULL)
            return NULL;

        TypeDescriptor const * target_type = target->resolved_type;
        if (target_type
            && (target_type->kind == TD_KIND_SLICE || target_type->kind == TD_KIND_DYNAMIC_ARRAY
                || target_type->kind == TD_KIND_MAP))
        {
            LLVMTypeRef struct_type = target_type->llvm_type;
            LLVMTypeRef val_type = LLVMTypeOf(ptr_val);
            LLVMValueRef data_ptr = NULL;
            if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
            {
                LLVMValueRef indices[]
                    = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                       LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                LLVMValueRef data_gep
                    = LLVMBuildInBoundsGEP2(ctx->builder, struct_type, ptr_val, indices, 2, "del.data.gep");
                data_ptr = LLVMBuildLoad2(
                    ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_gep, "del.data"
                );
            }
            else
            {
                LLVMValueRef extracted = LLVMBuildExtractValue(ctx->builder, ptr_val, 0, "del.data");
                data_ptr = LLVMBuildPointerCast(
                    ctx->builder, extracted, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), ""
                );
            }
            ir_gen_call_free(ctx, data_ptr);
        }
        else
        {
            // ir_gen_node auto-derefs pointers, giving the pointed-to value.
            // For delete, we need the raw POINTER VALUE to free.
            // Use lvalue to get the alloca, then load to get the pointer.
            LLVMValueRef lvalue = ir_gen_lvalue_ptr(ctx, target);
            if (lvalue != NULL)
            {
                LLVMTypeRef load_type
                    = target_type ? target_type->llvm_type : LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                LLVMValueRef ptr = LLVMBuildLoad2(ctx->builder, load_type, lvalue, "del.ptr");
                ir_gen_call_free(ctx, ptr);
            }
        }
        return NULL;
    }

    case AST_NODE_NIL:
    case AST_NODE_NONE:
        return ir_gen_nil(ctx, node);

    case AST_NODE_IDENTIFIER:
        return ir_gen_identifier(ctx, node);

    // Assignment expression — may contain an assignment operator
    case AST_NODE_ASSIGN_EXPRESSION:
        return ir_gen_assign_expression(ctx, node);

    // Postfix expression — handles calls through PostfixOps chain
    case AST_NODE_POSTFIX_EXPRESSION:
        return ir_gen_postfix_expression(ctx, node);

    // OrElseExpr and OrReturnExpr — handle with conditional branching
    case AST_NODE_OR_ELSE:
        return ir_gen_or_else_expression(ctx, node);
    case AST_NODE_OR_RETURN:
        return ir_gen_or_return_expression(ctx, node);

    // TernaryExpression — cond ? a : b (requires 3 children)
    case AST_NODE_TERNARY_EXPRESSION:
        return ir_gen_ternary_expression(ctx, node);

    // Wrapper expression nodes — delegate to first child
    case AST_NODE_EXPRESSION:
    case AST_NODE_PRIMARY_EXPRESSION:
        return ir_gen_expression(ctx, node);

    // Binary expression nodes — handle with operator dispatch
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
        return ir_gen_binary_expression(ctx, node);

    case AST_NODE_UNARY_EXPRESSION:
        return ir_gen_unary_expression(ctx, node);

    case AST_NODE_RETURN_STATEMENT:
        return ir_gen_return_statement(ctx, node);

    case AST_NODE_COMPOUND_STATEMENT:
        ctx->current_scope_depth++;
        generator_push_scope(ctx->gen_ctx);
        ir_gen_compound_statement(ctx, node);
        ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        ctx->current_scope_depth--;
        generator_pop_scope(ctx->gen_ctx);
        return NULL;

    case AST_NODE_EXPRESSION_STATEMENT:
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;

    case AST_NODE_ASSIGN_STATEMENT:
        return ir_gen_assign_statement(ctx, node);

    case AST_NODE_VARIABLE_DECL:
        return ir_gen_variable_decl(ctx, node);

    case AST_NODE_IF_STATEMENT:
    case AST_NODE_WHEN_STATEMENT:
        return ir_gen_if_statement(ctx, node);

    case AST_NODE_FOR_STATEMENT:
        return ir_gen_for_statement(ctx, node);

    case AST_NODE_SWITCH_STATEMENT:
        return ir_gen_switch_statement(ctx, node);

    case AST_NODE_BREAK_STATEMENT:
        if (ctx->loop_depth > 0)
        {
            int loop_scope = ctx->loop_stack[ctx->loop_depth - 1].scope_depth;
            ir_gen_emit_defers_from_depth(ctx, loop_scope + 1);
            LLVMBuildBr(ctx->builder, ctx->loop_stack[ctx->loop_depth - 1].break_bb);
        }
        return NULL;

    case AST_NODE_CONTINUE_STATEMENT:
        if (ctx->loop_depth > 0)
        {
            int loop_scope = ctx->loop_stack[ctx->loop_depth - 1].scope_depth;
            ir_gen_emit_defers_from_depth(ctx, loop_scope + 1);
            LLVMBuildBr(ctx->builder, ctx->loop_stack[ctx->loop_depth - 1].continue_bb);
        }
        return NULL;

    case AST_NODE_FALLTHROUGH_STATEMENT:
        if (ctx->fallthrough_target_bb)
        {
            LLVMBuildBr(ctx->builder, ctx->fallthrough_target_bb);
        }
        return NULL;

    case AST_NODE_DEFER_STATEMENT:
        if (node->list.count > 0 && ctx->defer_count < MAX_DEFERS)
        {
            ctx->defer_stack[ctx->defer_count].node = node->list.children[0];
            ctx->defer_stack[ctx->defer_count].scope_depth = ctx->current_scope_depth;
            ctx->defer_count++;
        }
        return NULL;

    case AST_NODE_CONSTANT_DECL:
        if (ctx->current_function != NULL)
            return ir_gen_nested_procedure_decl(ctx, node);
        return ir_gen_top_level_decl(ctx, node);

    default:
        return NULL;
    }

#pragma GCC diagnostic pop
}

// --- Main entry point ---

bool
ir_generate(IrGenContext * ctx, odin_grammar_node_t * ast)
{
    if (ctx == NULL || ast == NULL)
        return false;

    for (size_t i = 0; i < ast->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = ast->list.children[i];
        if (ext_decl == NULL || ext_decl->type != AST_NODE_EXTERNAL_DECLARATIONS)
            continue;

        for (size_t j = 0; j < ext_decl->list.count; j++)
        {
            odin_grammar_node_t * top_decl = ext_decl->list.children[j];
            if (top_decl == NULL)
                continue;

            if (top_decl->type == AST_NODE_CONSTANT_DECL)
            {
                ir_gen_top_level_decl(ctx, top_decl);
            }
            else if (top_decl->type == AST_NODE_VARIABLE_DECL)
            {
                ir_gen_top_level_variable(ctx, top_decl);
            }
        }
    }

    return !ir_gen_error_collection_has_errors(&ctx->errors);
}

// --- Output ---

int
write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path)
{
    char * ir_str = LLVMPrintModuleToString(module);
    if (ir_str == NULL)
        return -1;

    FILE * f = fopen(file_path, "w");
    if (f == NULL)
    {
        LLVMDisposeMessage(ir_str);
        return -1;
    }

    fprintf(f, "%s", ir_str);
    fclose(f);
    LLVMDisposeMessage(ir_str);
    return 0;
}

int
emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type)
{
    char const * triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target = NULL;
    char * error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error) != 0)
    {
        fprintf(stderr, "Error getting target: %s\n", error);
        LLVMDisposeMessage(error);
        return -1;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, march ? march : "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
    );

    if (LLVMTargetMachineEmitToFile(tm, module, file_path, file_type, &error) != 0)
    {
        fprintf(stderr, "Error emitting file: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        return -1;
    }

    LLVMDisposeTargetMachine(tm);
    return 0;
}
