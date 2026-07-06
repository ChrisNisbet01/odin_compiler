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
static bool ir_gen_node_contains_auto_cast(odin_grammar_node_t * node);
static LLVMValueRef ir_gen_or_else_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_or_return_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_ternary_expression(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_in_expression(
    IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef lhs, LLVMValueRef rhs, TypeDescriptor const * rhs_type, bool is_not_in
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
    ctx->foreign_libraries = NULL;
    ctx->foreign_library_count = 0;
    ctx->foreign_library_capacity = 0;
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
    for (int i = 0; i < ctx->foreign_library_count; i++)
        free(ctx->foreign_libraries[i]);
    free(ctx->foreign_libraries);
    free(ctx);
}

// --- Function context stack ---

static void
func_push(IrGenContext * ctx, LLVMValueRef func, TypeDescriptor const * return_type)
{
    if (ctx->func_depth < MAX_FUNC_DEPTH)
    {
        ctx->func_stack[ctx->func_depth].function = func;
        ctx->func_stack[ctx->func_depth].return_type = return_type;
        ctx->func_depth++;
    }
}

static void
func_pop(IrGenContext * ctx)
{
    if (ctx->func_depth > 0)
        ctx->func_depth--;
}

static LLVMValueRef
func_current_function(IrGenContext * ctx)
{
    return ctx->func_depth > 0 ? ctx->func_stack[ctx->func_depth - 1].function : NULL;
}

static void
ir_gen_implicit_return(IrGenContext * ctx)
{
    LLVMValueRef func = func_current_function(ctx);
    if (func == NULL)
        return;
    LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
    LLVMTypeRef ret_type = LLVMGetReturnType(func_type);
    if (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind)
    {
        LLVMBuildRetVoid(ctx->builder);
    }
    else
    {
        LLVMBuildRet(ctx->builder, LLVMConstNull(ret_type));
    }
}

// --- Auto-cast helper ---

static LLVMValueRef
ir_gen_auto_cast_value(IrGenContext * ctx, LLVMValueRef src_val, LLVMTypeRef target_type)
{
    if (src_val == NULL || target_type == NULL)
        return src_val;

    LLVMTypeRef src_llvm_type = LLVMTypeOf(src_val);
    LLVMTypeKind src_kind = LLVMGetTypeKind(src_llvm_type);
    LLVMTypeKind dest_kind = LLVMGetTypeKind(target_type);

    if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMIntegerTypeKind)
    {
        unsigned src_w = LLVMGetIntTypeWidth(src_llvm_type);
        unsigned dst_w = LLVMGetIntTypeWidth(target_type);
        if (dst_w > src_w)
            return LLVMBuildIntCast2(ctx->builder, src_val, target_type, false, "auto.zext");
        else if (dst_w < src_w)
            return LLVMBuildIntCast2(ctx->builder, src_val, target_type, false, "auto.trunc");
        return src_val;
    }
    else if ((src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind)
             && (dest_kind == LLVMFloatTypeKind || dest_kind == LLVMDoubleTypeKind))
    {
        return LLVMBuildFPCast(ctx->builder, src_val, target_type, "auto.fpcast");
    }
    else if (src_kind == LLVMIntegerTypeKind && (dest_kind == LLVMFloatTypeKind || dest_kind == LLVMDoubleTypeKind))
    {
        return LLVMBuildSIToFP(ctx->builder, src_val, target_type, "auto.sitofp");
    }
    else if ((src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind) && dest_kind == LLVMIntegerTypeKind)
    {
        return LLVMBuildFPToSI(ctx->builder, src_val, target_type, "auto.fptosi");
    }
    else if (src_kind == LLVMPointerTypeKind && dest_kind == LLVMIntegerTypeKind)
    {
        return LLVMBuildPtrToInt(ctx->builder, src_val, target_type, "auto.ptrint");
    }
    else if (src_kind == LLVMIntegerTypeKind && dest_kind == LLVMPointerTypeKind)
    {
        return LLVMBuildIntToPtr(ctx->builder, src_val, target_type, "auto.intptr");
    }
    return src_val;
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

    // Forward-declare procedures that haven't been code-generated yet
    if (sym->value.value == NULL && sym->value.type_info
        && sym->value.type_info->kind == TD_KIND_PROC)
    {
        sym->value.value = LLVMAddFunction(
            ctx->module, node->text, sym->value.type_info->proc_metadata.func_type
        );
    }

    if (sym->value.is_lvalue && sym->value.value != NULL)
    {
        // Don't load composite types — the pointer is needed for
        // GEP/subscript/member access
        if (sym->value.type_info
            && (sym->value.type_info->kind == TD_KIND_ARRAY || sym->value.type_info->kind == TD_KIND_SLICE
                || sym->value.type_info->kind == TD_KIND_STRUCT || sym->value.type_info->kind == TD_KIND_SOA
                || sym->value.type_info->kind == TD_KIND_DYNAMIC_ARRAY || sym->value.type_info->kind == TD_KIND_MAP
                || sym->value.type_info->kind == TD_KIND_BIT_FIELD || sym->value.type_info->kind == TD_KIND_UNION
                || sym->value.type_info->kind == TD_KIND_MAYBE))
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

    // Process escape sequences: first pass to count output length
    size_t arr_len = content_len + 1; // max possible (no escapes)
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);

    // First pass: count escaped length
    size_t escaped_len = 0;
    for (size_t i = 0; i < content_len; i++)
    {
        if (content[i] == '\\' && i + 1 < content_len)
        {
            switch (content[i + 1])
            {
            case 'n': case 't': case 'r': case '\\': case '"':
                escaped_len++; i++; break;
            default: escaped_len++; break;
            }
        }
        else
        {
            escaped_len++;
        }
    }
    arr_len = escaped_len + 1;

    LLVMValueRef * elements = malloc(arr_len * sizeof(LLVMValueRef));
    if (elements == NULL)
        return NULL;

    // Second pass: fill elements with escape processing
    size_t out_idx = 0;
    for (size_t i = 0; i < content_len; i++)
    {
        if (content[i] == '\\' && i + 1 < content_len)
        {
            switch (content[i + 1])
            {
            case 'n':  elements[out_idx++] = LLVMConstInt(i8_type, 0x0A, false); i++; break;
            case 't':  elements[out_idx++] = LLVMConstInt(i8_type, 0x09, false); i++; break;
            case 'r':  elements[out_idx++] = LLVMConstInt(i8_type, 0x0D, false); i++; break;
            case '\\': elements[out_idx++] = LLVMConstInt(i8_type, 0x5C, false); i++; break;
            case '"':  elements[out_idx++] = LLVMConstInt(i8_type, 0x22, false); i++; break;
            default:   elements[out_idx++] = LLVMConstInt(i8_type, (unsigned char)content[i], false); break;
            }
        }
        else
        {
            elements[out_idx++] = LLVMConstInt(i8_type, (unsigned char)content[i], false);
        }
    }
    elements[out_idx] = LLVMConstInt(i8_type, 0, false); // null terminator
    content_len = escaped_len;

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

/* --- In-expression (foundation) --- */

static LLVMValueRef
ir_gen_in_expression(
    IrGenContext * ctx, odin_grammar_node_t * node, LLVMValueRef lhs, LLVMValueRef rhs, TypeDescriptor const * rhs_type, bool is_not_in
)
{
    if (rhs_type == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "in/not_in: RHS has no type");
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
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "in/not_in: unsupported container type");
        return NULL;
    }

    if (elem_type == NULL || data_ptr == NULL || count_val == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "in/not_in: failed to extract element type, data, or count from container");
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
    {
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "binary expression has too few children");
        return NULL;
    }

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL)
    {
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "binary expression missing operator metadata");
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
        LLVMTypeKind lhs_tk = LLVMGetTypeKind(lhs_type);
        LLVMTypeKind rhs_tk = LLVMGetTypeKind(rhs_type);
        if (lhs_tk == LLVMIntegerTypeKind && rhs_tk == LLVMIntegerTypeKind)
        {
            unsigned lhs_bits = LLVMGetIntTypeWidth(lhs_type);
            unsigned rhs_bits = LLVMGetIntTypeWidth(rhs_type);
            bool sign_extend = (rhs_bits < lhs_bits);
            rhs = LLVMBuildIntCast2(ctx->builder, rhs, lhs_type, sign_extend, "coerce");
        }
        else if ((lhs_tk == LLVMHalfTypeKind || lhs_tk == LLVMFloatTypeKind || lhs_tk == LLVMDoubleTypeKind)
                 && (rhs_tk == LLVMHalfTypeKind || rhs_tk == LLVMFloatTypeKind || rhs_tk == LLVMDoubleTypeKind))
        {
            rhs = LLVMBuildFPCast(ctx->builder, rhs, lhs_type, "fltcoerce");
        }
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
    {
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "unary expression missing operator metadata");
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
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "unary expression has no operand");
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

static bool
init_contains_none(odin_grammar_node_t * node)
{
    if (node == NULL) return false;
    if (node->type == AST_NODE_NONE) return true;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (node->list.children[i] != NULL)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child->type == AST_NODE_NONE)
                return true;
            switch (child->type)
            {
                case AST_NODE_ASSIGN_EXPRESSION:
                case AST_NODE_OR_ELSE:
                case AST_NODE_OR_RETURN:
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
                case AST_NODE_POSTFIX_EXPRESSION:
                case AST_NODE_PRIMARY_EXPRESSION:
                    if (init_contains_none(child))
                        return true;
                    break;
                default:
                    break;
            }
        }
    }
    return false;
}

static LLVMValueRef
ir_gen_variable_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * id_list = node->list.children[0];
    if (id_list == NULL || id_list->type != AST_NODE_IDENTIFIER_LIST)
        return NULL;

    size_t id_count = id_list->list.count;

    // Find type and init children
    TypeDescriptor const * var_type = node->resolved_type;
    odin_grammar_node_t * type_node = NULL;
    odin_grammar_node_t * init_node = NULL;

    for (size_t i = 1; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if ((child->resolved_type || init_contains_none(child) || (child->type == AST_NODE_NIL))
            && child->type != AST_NODE_IDENTIFIER && child->type != AST_NODE_IDENTIFIER_LIST
            && !is_type_node(child))
            init_node = child;
        else if (is_type_node(child))
            type_node = child;
    }
    if (init_node == NULL)
    {
        for (size_t i = 1; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL) continue;
            if (init_contains_none(child) || child->type == AST_NODE_NIL)
            {
                init_node = child;
                break;
            }
        }
    }

    if (var_type == NULL && type_node)
    {
        var_type = type_node->resolved_type;
    }
    if (var_type == NULL && init_node)
    {
        var_type = init_node->resolved_type;
    }
    if (var_type == NULL)
    {
        var_type = type_descriptor_get_int64_type(ctx->type_registry);
    }

    // Multi-return destructuring: a, b := foo()
    if (id_count > 1 && init_node)
    {
        LLVMValueRef struct_val = ir_gen_node(ctx, init_node);
        if (struct_val == NULL)
            return NULL;

        for (size_t i = 0; i < id_count; i++)
        {
            odin_grammar_node_t * name_node = id_list->list.children[i];
            if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
                continue;

            LLVMValueRef field_val = LLVMBuildExtractValue(ctx->builder, struct_val, (unsigned)i, name_node->text);
            if (var_type->kind == TD_KIND_PROC && var_type->proc_metadata.return_count > (int)i)
            {
                TypeDescriptor const * field_type = var_type->proc_metadata.returns[i];
                LLVMTypeRef field_llvm = field_type ? field_type->llvm_type : LLVMTypeOf(field_val);
                LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, field_llvm, name_node->text);
                LLVMSetAlignment(alloca, LLVMABIAlignmentOfType(ctx->data_layout, field_llvm));
                LLVMBuildStore(ctx->builder, LLVMConstNull(field_llvm), alloca);
                LLVMBuildStore(ctx->builder, field_val, alloca);
                TypedValue tv = create_typed_value(alloca, field_type, true);
                generator_add_symbol(ctx->gen_ctx, name_node->text, tv);
            }
        }
        return struct_val;
    }

    // Single variable declaration
    odin_grammar_node_t * name_node = id_list->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    LLVMTypeRef llvm_type = var_type->llvm_type;
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, llvm_type, name_node->text);
    LLVMSetAlignment(alloca, LLVMABIAlignmentOfType(ctx->data_layout, llvm_type));

    LLVMBuildStore(ctx->builder, LLVMConstNull(llvm_type), alloca);

    if (init_node)
    {
        if (ir_gen_node_contains_auto_cast(init_node))
            ctx->auto_cast_target_type = var_type->llvm_type;
        LLVMValueRef init_val = ir_gen_node(ctx, init_node);
        ctx->auto_cast_target_type = NULL;
        if (init_val)
        {
            LLVMTypeRef init_llvm_type = LLVMTypeOf(init_val);
            // Auto-convert string struct {ptr, i64} → cstring ptr
            if (var_type && var_type->kind == TD_KIND_BASIC && var_type->as.basic.name != NULL
                && strcmp(var_type->as.basic.name, "cstring") == 0
                && LLVMGetTypeKind(init_llvm_type) == LLVMStructTypeKind)
            {
                init_val = LLVMBuildExtractValue(ctx->builder, init_val, 0, "str2cstr");
            }
            // Auto-convert integer types (e.g. int literal → i32 variable)
            if (var_type && LLVMGetTypeKind(var_type->llvm_type) == LLVMIntegerTypeKind
                && LLVMGetTypeKind(init_llvm_type) == LLVMIntegerTypeKind)
            {
                unsigned var_bits = LLVMGetIntTypeWidth(var_type->llvm_type);
                unsigned init_bits = LLVMGetIntTypeWidth(init_llvm_type);
                if (var_bits != init_bits)
                {
                    bool sign_extend = var_type->as.basic.is_unsigned ? false : (var_bits > init_bits);
                    init_val = LLVMBuildIntCast2(ctx->builder, init_val, var_type->llvm_type, sign_extend, "int2int");
                }
            }
            // Auto-convert float types (e.g. f64 literal → f16 variable)
            if (var_type && var_type->kind == TD_KIND_BASIC && var_type->as.basic.is_float)
            {
                LLVMTypeKind var_kind = LLVMGetTypeKind(var_type->llvm_type);
                LLVMTypeKind init_kind = LLVMGetTypeKind(init_llvm_type);
                if (var_kind != init_kind)
                {
                    if (var_kind == LLVMHalfTypeKind || var_kind == LLVMFloatTypeKind)
                    {
                        init_val = LLVMBuildFPTrunc(ctx->builder, init_val, var_type->llvm_type, "flt2flt");
                    }
                    else
                    {
                        init_val = LLVMBuildFPExt(ctx->builder, init_val, var_type->llvm_type, "flt2flt");
                    }
                }
            }
            if (var_type && var_type->kind == TD_KIND_MAYBE)
            {
                LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                if (init_node && init_contains_none(init_node))
                {
                    // none: set tag = 1 (payload is already zeroed from zero-init)
                    LLVMValueRef tag_indices[2] = {idx0, idx0};
                    LLVMValueRef tag_gep = LLVMBuildInBoundsGEP2(ctx->builder, var_type->llvm_type, alloca, tag_indices, 2, "maybe.tag.gep");
                    LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false), tag_gep);
                }
                else if (init_val != NULL)
                {
                    // some(value): set tag = 0 (already zero), store payload
                    LLVMValueRef payload_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
                    LLVMValueRef payload_gep = LLVMBuildInBoundsGEP2(ctx->builder, var_type->llvm_type, alloca, payload_indices, 2, "maybe.payload.gep");
                    LLVMBuildStore(ctx->builder, init_val, payload_gep);
                }
            }
            else if (var_type && var_type->as.basic.name && strcmp(var_type->as.basic.name, "any") == 0)
            {
                // If the initializer is already an any struct, store directly
                if (init_node->resolved_type == var_type)
                {
                    LLVMTypeRef ivt = LLVMTypeOf(init_val);
                    if (LLVMGetTypeKind(ivt) == LLVMPointerTypeKind)
                        init_val = LLVMBuildLoad2(ctx->builder, var_type->llvm_type, init_val, "loadtmp");
                    LLVMBuildStore(ctx->builder, init_val, alloca);
                }
                else
                {
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
                    LLVMValueRef zero_idx = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    LLVMValueRef data_ptr;
                    LLVMTypeRef val_type = LLVMTypeOf(init_val);
                    LLVMTypeKind val_kind = LLVMGetTypeKind(val_type);
                    if (val_kind == LLVMPointerTypeKind)
                    {
                        data_ptr = LLVMBuildBitCast(ctx->builder, init_val, i8ptr, "anydata");
                    }
                    else
                    {
                        LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, val_type, "anytmp");
                        LLVMBuildStore(ctx->builder, init_val, tmp);
                        data_ptr = LLVMBuildBitCast(ctx->builder, tmp, i8ptr, "anydata");
                    }
                    int64_t init_tid = (init_node->resolved_type != NULL) ? (int64_t)init_node->resolved_type->type_id : 0;
                    LLVMValueRef type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), init_tid, false);
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
            }
            else if (init_val != NULL)
            {
                LLVMBuildStore(ctx->builder, init_val, alloca);
            }
        }
    }

    TypedValue tv = create_typed_value(alloca, var_type, true);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

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
        // _ (blank identifier) — silently OK, used for discards
        if (node->text && strcmp(node->text, "_") == 0)
            return NULL;
        if (sym == NULL)
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "undeclared identifier in lvalue context");
        else
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "identifier is not an lvalue");
        return NULL;
    }

    case AST_NODE_CONTEXT_EXPR:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
        if (sym && sym->value.is_lvalue)
            return sym->value.value;
        if (node->text && strcmp(node->text, "_") == 0)
            return NULL;
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "'context' is not available in this scope");
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
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "subscript index expression is NULL");
                    return NULL;
                }

                LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
                if (index_val == NULL)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "failed to evaluate subscript index");
                    return NULL;
                }

                if (cur_type == NULL)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "subscript target has unknown type");
                    return NULL;
                }

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
                else if (cur_type->kind == TD_KIND_BASIC && cur_type->as.basic.name != NULL
                         && strcmp(cur_type->as.basic.name, "string") == 0)
                {
                    // String subscript: extract data ptr from {ptr, i64} struct, GEP, store ptr for load
                    LLVMValueRef data_indices[]
                        = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                           LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                    LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
                        ctx->builder, cur_type->llvm_type, ptr, data_indices, 2, "str.data.ptr"
                    );
                    LLVMValueRef data = LLVMBuildLoad2(
                        ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_field, "str.data"
                    );
                    ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, LLVMInt8TypeInContext(ctx->context), data, &index_val, 1, "str.subs"
                    );
                    cur_type = get_basic_type_by_name(ctx->type_registry, "u8");
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
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.loop");
                    LLVMBasicBlockRef body_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.body");
                    LLVMBasicBlockRef kchk_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.kchk");
                    LLVMBasicBlockRef found_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.found");
                    LLVMBasicBlockRef next_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.next");
                    LLVMBasicBlockRef empty_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.empty");
                    LLVMBasicBlockRef after_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.after");
                    LLVMBasicBlockRef claim_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.claim");
                    LLVMBasicBlockRef merge_bb
                        = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "m.merge");

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
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "map subscript: failed to extract data pointer");
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
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "member access: missing field name");
                    return NULL;
                }

                if (cur_type == NULL)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "member access: current type is unknown");
                    return NULL;
                }

                if (cur_type->kind == TD_KIND_UNION)
                {
                    int field_idx = type_descriptor_find_union_field_index(cur_type, field_name_node->text);
                    if (field_idx < 0)
                    {
                        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "union has no field named");
                        return NULL;
                    }
                    struct_field_t const * field = type_descriptor_get_union_field(cur_type, field_idx);
                    if (field == NULL)
                    {
                        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "union field lookup returned NULL");
                        return NULL;
                    }

                    // Set tag field
                    LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    LLVMValueRef tag_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
                    LLVMValueRef tag_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, cur_type->llvm_type, ptr, tag_indices, 2, "union.tag.gep"
                    );
                    LLVMValueRef tag_val
                        = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)field_idx, false);
                    LLVMBuildStore(ctx->builder, tag_val, tag_ptr);

                    // Bitcast payload pointer to field type
                    LLVMValueRef payload_indices[2]
                        = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
                    LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
                        ctx->builder, cur_type->llvm_type, ptr, payload_indices, 2, "union.payload.gep"
                    );
                    ptr = LLVMBuildPointerCast(
                        ctx->builder,
                        payload_ptr,
                        LLVMPointerType(field->type_desc->llvm_type, 0),
                        field_name_node->text
                    );
                    cur_type = field->type_desc;
                    break;
                }

                if (cur_type->kind == TD_KIND_MAYBE)
                {
                    // Maybe(T).value — access payload field 1, bitcast to inner type
                    LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                    LLVMValueRef payload_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
                    LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, ptr, payload_indices, 2, "maybe.val.gep");
                    ptr = LLVMBuildBitCast(ctx->builder, payload_ptr, LLVMPointerType(cur_type->as.maybe.inner_type->llvm_type, 0), field_name_node->text);
                    cur_type = cur_type->as.maybe.inner_type;
                    break;
                }

                if (cur_type->kind != TD_KIND_STRUCT && cur_type->kind != TD_KIND_SOA)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "member access: type is not a struct");
                    return NULL;
                }

                field_access_path_t path;
                if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "struct has no field named");
                    return NULL;
                }

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
                {
                    if (cur_type)
                        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot dereference non-pointer type in lvalue context");
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

        // Merge with phi
        LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(ctx->builder, inner_llvm, "or.phi");
        LLVMValueRef phi_vals[2] = {payload, rhs};
        LLVMBasicBlockRef phi_blocks[2] = {entry_bb, rhs_bb};
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

    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.ret");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "or.cont");

    LLVMBuildCondBr(ctx->builder, is_zero, ret_bb, cont_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, ret_bb);
    ir_gen_emit_all_defers(ctx);
    ir_gen_implicit_return(ctx);

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
static void
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
    odin_grammar_node_t * t = lhs_expr;
    while (t && t->list.count >= 1 && t->list.children[0])
        t = t->list.children[0];
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
    odin_grammar_node_t * rt = rhs_node;
    while (rt && rt->list.count >= 1 && rt->list.children[0])
        rt = rt->list.children[0];
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

    if (ir_gen_bit_field_write(ctx, node->list.children[0], rhs_val, op_kind))
        return rhs_val;

    if (ir_gen_bit_set_assign_expr(ctx, node->list.children[0], node->list.children[2], rhs_val, op_kind))
        return rhs_val;

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
        TypeDescriptor const * rhs_type
            = (node->list.count > 2 && node->list.children[2]) ? node->list.children[2]->resolved_type : NULL;
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

    if (ir_gen_bit_field_write(ctx, node->list.children[0], rhs_val, op_kind))
        return rhs_val;

    if (ir_gen_bit_set_assign_expr(ctx, node->list.children[0], node->list.children[2], rhs_val, op_kind))
        return rhs_val;

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
        TypeDescriptor const * rhs_type
            = (node->list.count > 2 && node->list.children[2]) ? node->list.children[2]->resolved_type : NULL;
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

static bool
ir_gen_node_contains_auto_cast(odin_grammar_node_t * node)
{
    if (node == NULL)
        return false;
    if (node->type == AST_NODE_AUTO_CAST_EXPR)
        return true;
    for (size_t ci = 0; ci < node->list.count; ci++)
    {
        if (ir_gen_node_contains_auto_cast(node->list.children[ci]))
            return true;
    }
    return false;
}

// --- Statement codegen ---

static LLVMValueRef
ir_gen_return_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // Collect all expression children (may be multiple for multi-return)
    int expr_count = 0;
    odin_grammar_node_t * exprs[16];
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && expr_count < 16)
        {
            exprs[expr_count++] = child;
        }
    }

    // Evaluate return values first (so defers can be emitted after)
    LLVMValueRef vals[16];
    for (int i = 0; i < expr_count; i++)
    {
        if (expr_count == 1 && ir_gen_node_contains_auto_cast(exprs[i]))
        {
            LLVMValueRef func = func_current_function(ctx);
            LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
            ctx->auto_cast_target_type = LLVMGetReturnType(func_type);
        }
        vals[i] = ir_gen_node(ctx, exprs[i]);
        ctx->auto_cast_target_type = NULL;
        if (vals[i] == NULL && expr_count > 0)
            return NULL;
    }

    // Emit all pending defers before the return instruction
    ir_gen_emit_all_defers(ctx);

    if (expr_count == 0)
    {
        return LLVMBuildRetVoid(ctx->builder);
    }

    if (expr_count == 1)
    {
        return LLVMBuildRet(ctx->builder, vals[0]);
    }

    // Multi-return: pack values into struct
    LLVMValueRef func = func_current_function(ctx);
    LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
    LLVMTypeRef ret_struct = LLVMGetReturnType(func_type);

    LLVMValueRef struct_val = LLVMGetUndef(ret_struct);
    for (int i = 0; i < expr_count; i++)
    {
        struct_val = LLVMBuildInsertValue(ctx->builder, struct_val, vals[i], (unsigned)i, "ret.field");
    }
    return LLVMBuildRet(ctx->builder, struct_val);
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

    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "then");
    LLVMBasicBlockRef else_bb
        = else_node ? LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "else") : NULL;
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "ifmerge");

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
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forcond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forbody");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forinc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forend");

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

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forcond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forbody");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forend");

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

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "swend");

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
        case_bbs[i] = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "case");
    }
    if (default_case)
    {
        default_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "default");
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
            miss_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "swnext");
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

    // If calling convention is ODIN, param 0 is the implicit context pointer
    unsigned param_index = 0;
    if (proc_literal->resolved_type && proc_literal->resolved_type->kind == TD_KIND_PROC
        && proc_literal->resolved_type->proc_metadata.calling_convention == CALLING_CONV_ODIN)
    {
        param_index = 1;
    }
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

// --- Foreign library registration ---

static void
ir_gen_add_foreign_library(IrGenContext * ctx, char const * lib_path)
{
    if (ctx == NULL || lib_path == NULL || lib_path[0] == '\0')
        return;
    if (ctx->foreign_library_count >= ctx->foreign_library_capacity)
    {
        int new_cap = ctx->foreign_library_capacity == 0 ? 8 : ctx->foreign_library_capacity * 2;
        char ** new_libs = realloc(ctx->foreign_libraries, (size_t)new_cap * sizeof(char *));
        if (new_libs == NULL)
            return;
        ctx->foreign_libraries = new_libs;
        ctx->foreign_library_capacity = new_cap;
    }
    ctx->foreign_libraries[ctx->foreign_library_count] = strdup(lib_path);
    ctx->foreign_library_count++;
}

static void
ir_gen_collect_foreign_import(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL || node->type != AST_NODE_FOREIGN_IMPORT)
        return;
    // Children: [0] = Identifier (name), [1] = StringLiteral (path)
    if (node->list.count < 2)
        return;
    odin_grammar_node_t * path_node = node->list.children[1];
    if (path_node == NULL || path_node->text == NULL)
        return;
    // Strip surrounding quotes from string literal text
    char const * src = path_node->text;
    size_t len = strlen(src);
    if (len >= 2 && src[0] == '"' && src[len - 1] == '"')
    {
        char * stripped = strndup(src + 1, len - 2);
        ir_gen_add_foreign_library(ctx, stripped);
        free(stripped);
    }
    else
    {
        ir_gen_add_foreign_library(ctx, src);
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

        LLVMValueRef func = LLVMGetNamedFunction(ctx->module, name_node->text);
        if (func == NULL)
            func = LLVMAddFunction(ctx->module, name_node->text, proc_type->proc_metadata.func_type);

        odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);

        if (body_node)
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, entry);

            func_push(ctx, func, proc_type->proc_metadata.return_type);

            generator_push_scope(ctx->gen_ctx);
            ir_gen_register_params(ctx, value_node, func);

            // Inject implicit context parameter for ODIN calling convention
            if (proc_type->proc_metadata.calling_convention == CALLING_CONV_ODIN)
            {
                LLVMValueRef context_param = LLVMGetParam(func, 0);
                TypeDescriptor const * ctx_type = type_descriptor_get_context_type(ctx->type_registry);
                if (ctx_type)
                {
                    LLVMValueRef context_alloca = LLVMBuildAlloca(ctx->builder, ctx_type->llvm_type, "context");
                    LLVMValueRef size_val = LLVMConstInt(
                        LLVMInt64TypeInContext(ctx->context),
                        (long long)LLVMABISizeOfType(ctx->data_layout, ctx_type->llvm_type),
                        false
                    );
                    LLVMBuildMemCpy(ctx->builder, context_alloca, 0, context_param, 0, size_val);

                    symbol_t * ctx_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
                    if (ctx_sym)
                    {
                        ctx_sym->value.value = context_alloca;
                    }
                    else
                    {
                        TypedValue ctx_tv = create_typed_value(context_alloca, ctx_type, true);
                        generator_add_symbol(ctx->gen_ctx, "context", ctx_tv);
                    }
                }
            }

            ir_gen_node(ctx, body_node);

            LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
            LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
            if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
            {
                ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
                ir_gen_implicit_return(ctx);
            }

            generator_pop_scope(ctx->gen_ctx);

            func_pop(ctx);
        }

        TypedValue tv = create_typed_value(func, proc_type, false);
        generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

        return func;
    }

    // Non-procedure constant: evaluate and store LLVM constant value
    LLVMValueRef const_val = ir_gen_node(ctx, value_node);
    if (const_val)
    {
        TypedValue tv = create_typed_value(const_val, value_node->resolved_type, false);
        generator_add_symbol(ctx->gen_ctx, name_node->text, tv);
        return const_val;
    }

    return NULL;
}

static LLVMValueRef
ir_gen_top_level_variable(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * id_list = node->list.children[0];
    if (id_list == NULL || id_list->type != AST_NODE_IDENTIFIER_LIST)
        return NULL;

    for (size_t vi = 0; vi < id_list->list.count; vi++)
    {
        odin_grammar_node_t * name_node = id_list->list.children[vi];
        if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
            continue;

        TypeDescriptor const * var_type = node->resolved_type;
        if (var_type == NULL)
        {
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
        if (var_type == NULL)
        {
            var_type = type_descriptor_get_int64_type(ctx->type_registry);
        }

        LLVMTypeRef llvm_type = var_type->llvm_type;
        LLVMValueRef global = LLVMAddGlobal(ctx->module, llvm_type, name_node->text);

        if (vi == 0)
        {
            if (node->list.count >= 3)
            {
                odin_grammar_node_t * init_node = node->list.children[2];
                if (init_node)
                {
                    LLVMValueRef init_val = ir_gen_node(ctx, init_node);
                    if (init_val)
                        LLVMSetInitializer(global, init_val);
                }
            }
            else if (node->list.count == 2)
            {
                odin_grammar_node_t * second = node->list.children[1];
                if (second && !is_type_node(second))
                {
                    LLVMValueRef init_val = ir_gen_node(ctx, second);
                    if (init_val)
                        LLVMSetInitializer(global, init_val);
                }
            }
        }

        TypedValue tv = create_typed_value(global, var_type, true);
        generator_add_symbol(ctx->gen_ctx, name_node->text, tv);
    }

    return NULL;
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

    // Save outer insertion block
    LLVMBasicBlockRef outer_block = LLVMGetInsertBlock(ctx->builder);

    // Build nested function
    LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, proc_type->proc_metadata.func_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    func_push(ctx, func, proc_type->proc_metadata.return_type);

    generator_push_scope(ctx->gen_ctx);
    ir_gen_register_params(ctx, value_node, func);

    // Inject implicit context parameter for ODIN calling convention
    if (proc_type->proc_metadata.calling_convention == CALLING_CONV_ODIN)
    {
        LLVMValueRef context_param = LLVMGetParam(func, 0);
        TypeDescriptor const * ctx_type = type_descriptor_get_context_type(ctx->type_registry);
        if (ctx_type)
        {
            LLVMValueRef context_alloca = LLVMBuildAlloca(ctx->builder, ctx_type->llvm_type, "context");
            LLVMValueRef size_val = LLVMConstInt(
                LLVMInt64TypeInContext(ctx->context),
                (long long)LLVMABISizeOfType(ctx->data_layout, ctx_type->llvm_type),
                false
            );
            LLVMBuildMemCpy(ctx->builder, context_alloca, 0, context_param, 0, size_val);

            symbol_t * ctx_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
            if (ctx_sym)
            {
                ctx_sym->value.value = context_alloca;
            }
            else
            {
                TypedValue ctx_tv = create_typed_value(context_alloca, ctx_type, true);
                generator_add_symbol(ctx->gen_ctx, "context", ctx_tv);
            }
        }
    }

    odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);
    if (body_node)
    {
        ir_gen_node(ctx, body_node);
    }
    else
    {
        // No body — this is a declaration without definition, return early
        generator_pop_scope(ctx->gen_ctx);
        func_pop(ctx);
        LLVMPositionBuilderAtEnd(ctx->builder, outer_block);
        return func;
    }

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
    if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
    {
        ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        ir_gen_implicit_return(ctx);
    }

    generator_pop_scope(ctx->gen_ctx);

    // Register in current (enclosing) scope
    TypedValue tv = create_typed_value(func, proc_type, false);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

    // Restore outer function context
    func_pop(ctx);
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
            args[count] = ir_gen_node(ctx, last);
            if (arg_types)
                arg_types[count] = last->resolved_type;
            count++;
        }
        return count;
    }

    // Single expression — evaluate directly
    args[0] = ir_gen_node(ctx, node);
    if (arg_types)
        arg_types[0] = node->resolved_type;
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

    odin_grammar_node_t * pe_child = node->list.children[0];
    TypeDescriptor const * cur_type = pe_child ? pe_child->resolved_type : NULL;

    if (cur_type == NULL && pe_child != NULL)
    {
        odin_grammar_node_t * ident = expression_unwrap_to_identifier(pe_child);
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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "called value is not a procedure");
                return val;
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
                            // Also skip if the LLVM value type matches any struct type
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

            val = LLVMBuildCall2(
                ctx->builder,
                func_type,
                val,
                args,
                (unsigned)arg_count,
                proc_type->proc_metadata.is_void_return ? "" : "calltmp"
            );

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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "missing index expression in subscript");
                break;
            }

            LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
            if (index_val == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, index_expr, "invalid index expression in subscript");
                break;
            }

            if (cur_type == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot subscript unknown type");
                break;
            }

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
            else if (cur_type->kind == TD_KIND_BASIC && cur_type->as.basic.name != NULL
                     && strcmp(cur_type->as.basic.name, "string") == 0)
            {
                LLVMValueRef struct_val;
                LLVMTypeRef val_type = LLVMTypeOf(val);
                if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
                    struct_val = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "str.load");
                else
                    struct_val = val;

                LLVMValueRef data = LLVMBuildExtractValue(ctx->builder, struct_val, 0, "str.data");
                LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, LLVMInt8TypeInContext(ctx->context), data, &index_val, 1, "str.subs"
                );
                val = LLVMBuildLoad2(ctx->builder, LLVMInt8TypeInContext(ctx->context), elem_ptr, "str.char");
                cur_type = get_basic_type_by_name(ctx->type_registry, "u8");
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
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "map subscript: data pointer extraction failed");
                    break;
                }

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
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.loop");
                LLVMBasicBlockRef body_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.body");
                LLVMBasicBlockRef kchk_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.kchk");
                LLVMBasicBlockRef found_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.found");
                LLVMBasicBlockRef next_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.next");
                LLVMBasicBlockRef notfound_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.notfound");
                LLVMBasicBlockRef merge_bb
                    = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "mr.merge");

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
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot subscript type: not an array, slice, dynamic array, or map");
                break;
            }

            if (cur_type->kind == TD_KIND_MAP && op->resolved_type)
                cur_type = op->resolved_type;
            else if (cur_type->element_type)
                cur_type = cur_type->element_type;
            else if (op->resolved_type)
                cur_type = op->resolved_type;
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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "missing field name in member access");
                break;
            }

            // Package-qualified access: pkg.member (resolved by semantic analyser)
            if (cur_type == NULL && op->resolved_symbol != NULL)
            {
                val = op->resolved_symbol->value.value;
                cur_type = op->resolved_symbol->value.type_info;
                break;
            }

            // Maybe(T).value — access payload field
            if (cur_type && cur_type->kind == TD_KIND_MAYBE && field_name_node->text
                && strcmp(field_name_node->text, "value") == 0)
            {
                LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, val,
                    (LLVMValueRef[]) {
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)
                    }, 2, "maybe.val.gep"
                );
                val = LLVMBuildLoad2(ctx->builder, cur_type->as.maybe.inner_type->llvm_type, payload_ptr, "maybe.val.load");
                cur_type = cur_type->as.maybe.inner_type;
                break;
            }

            if (cur_type == NULL
                || (cur_type->kind != TD_KIND_STRUCT && cur_type->kind != TD_KIND_SOA && cur_type->kind != TD_KIND_UNION
                ))
            {
                if (cur_type && cur_type->kind == TD_KIND_BIT_FIELD)
                {
                    char const * field_name = field_name_node->text;
                    bit_field_field_info const * bf = type_descriptor_find_bit_field_field(cur_type, field_name);
                    if (bf == NULL)
                    {
                        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "bit_field has no field named");
                        break;
                    }

                    LLVMValueRef backing = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "bf.backing");
                    LLVMValueRef shifted = backing;
                    if (bf->offset_bits > 0)
                    {
                        LLVMValueRef off = LLVMConstInt(cur_type->llvm_type, (unsigned)bf->offset_bits, false);
                        shifted = LLVMBuildLShr(ctx->builder, backing, off, "bf.shifted");
                    }
                    uint64_t mask_val = (bf->width_bits >= 64) ? ~0ULL : ((1ULL << bf->width_bits) - 1);
                    LLVMValueRef mask = LLVMConstInt(cur_type->llvm_type, mask_val, false);
                    LLVMValueRef extracted = LLVMBuildAnd(ctx->builder, shifted, mask, "bf.extracted");
                    val = LLVMBuildIntCast(ctx->builder, extracted, bf->type->llvm_type, "bf.val");
                    cur_type = bf->type;
                }
                else if (cur_type)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "type has no member");
                }
                break;
            }

            if (cur_type->kind == TD_KIND_UNION)
            {
                int field_idx = type_descriptor_find_union_field_index(cur_type, field_name_node->text);
                if (field_idx < 0)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "union has no field named");
                    break;
                }
                struct_field_t const * field = type_descriptor_get_union_field(cur_type, field_idx);
                if (field == NULL)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "union field lookup failed");
                    break;
                }

                // Bitcast payload pointer to field type
                LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                LLVMValueRef payload_indices[2] = {idx0, LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
                LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, val, payload_indices, 2, "union.payload.gep"
                );
                val = LLVMBuildPointerCast(
                    ctx->builder, payload_ptr, LLVMPointerType(field->type_desc->llvm_type, 0), field_name_node->text
                );
                cur_type = field->type_desc;
                break;
            }

            field_access_path_t path;
            if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "type has no field");
                break;
            }

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
            {
                if (cur_type)
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot dereference non-pointer type");
                break;
            }
            TypeDescriptor const * pointee_type = cur_type->pointee;
            if (pointee_type == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot dereference pointer to void");
                break;
            }
            val = LLVMBuildLoad2(ctx->builder, pointee_type->llvm_type, val, "deref");
            cur_type = pointee_type;
            break;
        }

        case AST_NODE_POSTFIX_ASSERTION:
        {
            TypeDescriptor const * target_type = op->resolved_type;
            if (target_type == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "type assertion has no resolved target type");
                break;
            }

            // Type assertion x.(T) for 'any': extract data pointer, bitcast, load
            if (cur_type && cur_type->kind == TD_KIND_BASIC && cur_type->as.basic.name
                && strcmp(cur_type->as.basic.name, "any") == 0)
            {
                // Store the any struct to alloca so we can GEP into it
                LLVMTypeRef any_val_type = LLVMTypeOf(val);
                if (LLVMGetTypeKind(any_val_type) == LLVMPointerTypeKind)
                    val = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "assert.any.load");
                LLVMValueRef tmp_alloca = LLVMBuildAlloca(ctx->builder, cur_type->llvm_type, "assert.tmp");
                LLVMBuildStore(ctx->builder, val, tmp_alloca);
                LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
                // Extract type_id field (field 1) for runtime type check
                LLVMValueRef gep_id[2] = {idx0, idx1};
                LLVMValueRef id_field = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, tmp_alloca, gep_id, 2, "assert.typeid.ptr"
                );
                LLVMValueRef stored_type_id
                    = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), id_field, "assert.typeid");
                int64_t expected_tid = (int64_t)target_type->type_id;
                LLVMValueRef expected_type_id = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), expected_tid, false);
                LLVMValueRef type_match
                    = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_type_id, expected_type_id, "assert.match");
                // Create blocks for match/fail/continue
                LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
                LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.match");
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.fail");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "assert.cont");
                LLVMBuildCondBr(ctx->builder, type_match, match_bb, fail_bb);
                // --- Match block: extract data ---
                LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
                LLVMValueRef gep_data[2] = {idx0, idx0};
                LLVMValueRef data_field = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, tmp_alloca, gep_data, 2, "assert.data.ptr"
                );
                LLVMValueRef data_ptr = LLVMBuildLoad2(
                    ctx->builder, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), data_field, "assert.data"
                );
                if (target_type->kind == TD_KIND_BASIC && !target_type->as.basic.is_float
                    && target_type->as.basic.width > 0 && target_type->as.basic.width <= 64)
                {
                    LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, data_ptr,
                        LLVMPointerType(target_type->llvm_type, 0), "assert.typed");
                    val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "assert.val");
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
                LLVMBuildBr(ctx->builder, cont_bb);
                // --- Fail block: trap ---
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
                // --- Continue block ---
                LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
            }
            else if (cur_type && cur_type->kind == TD_KIND_MAYBE)
            {
                // Type assertion x.(T) for Maybe(T): check tag == 0 (some)
                LLVMValueRef ptr = val;
                LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
                LLVMValueRef tag_indices[2] = {idx0, idx0};
                LLVMValueRef tag_ptr = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, ptr, tag_indices, 2, "massert.tag.gep");
                LLVMValueRef stored_tag = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), tag_ptr, "massert.tag");
                LLVMValueRef expected_tag = LLVMConstNull(LLVMInt64TypeInContext(ctx->context));
                LLVMValueRef tag_match = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_tag, expected_tag, "massert.match");
                LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
                LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.match");
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.fail");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "massert.cont");
                LLVMBuildCondBr(ctx->builder, tag_match, match_bb, fail_bb);
                // --- Match block: extract payload ---
                LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
                LLVMValueRef payload_indices[2] = {idx0, idx1};
                LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, ptr, payload_indices, 2, "massert.payload.gep");
                LLVMValueRef typed_ptr = LLVMBuildBitCast(ctx->builder, payload_ptr, LLVMPointerType(target_type->llvm_type, 0), "massert.typed");
                val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "massert.val");
                cur_type = target_type;
                LLVMBuildBr(ctx->builder, cont_bb);
                // --- Fail block: trap ---
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
                // --- Continue block ---
                LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
            }
            else if (cur_type && cur_type->kind == TD_KIND_UNION)
            {
                // Type assertion x.(T) for union: check tag vs field index
                int field_idx = (int)(intptr_t)op->resolved_symbol;
                if (field_idx < 0)
                {
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "union type assertion field not found");
                    break;
                }

                // val is a pointer to the union struct {i64 tag, [N x i8] payload}
                LLVMValueRef ptr = val;
                LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
                // Extract tag (field 0) for runtime tag check
                LLVMValueRef tag_indices[2] = {idx0, idx0};
                LLVMValueRef tag_ptr
                    = LLVMBuildInBoundsGEP2(ctx->builder, cur_type->llvm_type, ptr, tag_indices, 2, "uassert.tag.gep");
                LLVMValueRef stored_tag
                    = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), tag_ptr, "uassert.tag");
                LLVMValueRef expected_tag
                    = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)field_idx, false);
                LLVMValueRef tag_match
                    = LLVMBuildICmp(ctx->builder, LLVMIntEQ, stored_tag, expected_tag, "uassert.match");
                // Create blocks for match/fail/continue
                LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
                LLVMValueRef current_func = LLVMGetBasicBlockParent(current_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.match");
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.fail");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "uassert.cont");
                LLVMBuildCondBr(ctx->builder, tag_match, match_bb, fail_bb);
                // --- Match block: extract payload ---
                LLVMPositionBuilderAtEnd(ctx->builder, match_bb);
                LLVMValueRef payload_indices[2] = {idx0, idx1};
                LLVMValueRef payload_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, ptr, payload_indices, 2, "uassert.payload.gep"
                );
                LLVMValueRef typed_ptr = LLVMBuildPointerCast(
                    ctx->builder, payload_ptr, LLVMPointerType(target_type->llvm_type, 0), "uassert.typed"
                );
                val = LLVMBuildLoad2(ctx->builder, target_type->llvm_type, typed_ptr, "uassert.val");
                cur_type = target_type;
                LLVMBuildBr(ctx->builder, cont_bb);
                // --- Fail block: trap ---
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
                // --- Continue block ---
                LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);
            }
            break;
        }

        case AST_NODE_POSTFIX_SLICE:
        case AST_NODE_POSTFIX_SLICE_LT:
        {
            if (cur_type == NULL || (cur_type->kind != TD_KIND_SLICE && cur_type->kind != TD_KIND_ARRAY))
            {
                if (cur_type)
                    ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "cannot slice: type is not a slice or array");
                break;
            }

            TypeDescriptor const * slice_type = cur_type;
            TypeDescriptor const * elem_type = slice_type->element_type;
            if (elem_type == NULL)
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, op, "slice/array has no element type");
                break;
            }

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
                && cur_type->kind != TD_KIND_UNION && !is_ptr_valued_basic)
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

    case AST_NODE_AUTO_CAST_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        LLVMValueRef src_val = ir_gen_node(ctx, node->list.children[0]);
        if (src_val == NULL)
            return NULL;

        LLVMTypeRef target = ctx->auto_cast_target_type;
        ctx->auto_cast_target_type = NULL;
        if (target == NULL)
            return src_val;
        return ir_gen_auto_cast_value(ctx, src_val, target);
    }

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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "cast expression has no resolved target type");
            return NULL;
        }

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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "transmute expression has no resolved target type");
            return NULL;
        }

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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "len/cap operand has no type");
            return NULL;
        }

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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "len/cap: failed to extract map data pointer");
                return NULL;
            }
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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "len/cap: failed to extract field from slice/dynamic array");
            return NULL;
        }
        return len_val;
    }

    case AST_NODE_PRINT_STRING_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand = node->list.children[0];
        LLVMValueRef str_val = ir_gen_node(ctx, operand);
        if (str_val == NULL)
            return NULL;

        TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
        if (str_desc == NULL || str_desc->llvm_type == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "print_string: string type not found");
            return NULL;
        }
        LLVMTypeRef str_struct_type = str_desc->llvm_type;

        // Extract data pointer (field 0) and length (field 1)
        LLVMValueRef str_struct = NULL;
        LLVMTypeRef val_type = LLVMTypeOf(str_val);
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
        {
            str_struct = LLVMBuildLoad2(ctx->builder, str_struct_type, str_val, "print.str");
        }
        else
        {
            str_struct = str_val;
        }

        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, str_struct, 0, "print.data");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, str_struct, 1, "print.len");

        // Declare putchar if not already declared
        LLVMTypeRef putchar_param_types[] = {LLVMInt32TypeInContext(ctx->context)};
        LLVMTypeRef putchar_ftype = LLVMFunctionType(
            LLVMInt32TypeInContext(ctx->context), putchar_param_types, 1, false
        );
        LLVMValueRef putchar_fn = LLVMGetNamedFunction(ctx->module, "putchar");
        if (putchar_fn == NULL)
        {
            putchar_fn = LLVMAddFunction(ctx->module, "putchar", putchar_ftype);
            LLVMSetLinkage(putchar_fn, LLVMExternalLinkage);
        }

        // Loop: for i64 i = 0; i < len; i++
        LLVMValueRef current_func = func_current_function(ctx);
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print.loop");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "print.end");

        LLVMValueRef i_alloca = LLVMBuildAlloca(ctx->builder, LLVMInt64TypeInContext(ctx->context), "print.i");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false), i_alloca);
        LLVMBuildBr(ctx->builder, loop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, loop_bb);
        LLVMValueRef i_val = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), i_alloca, "print.i.val");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, len_val, "print.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        LLVMValueRef char_ptr = LLVMBuildInBoundsGEP2(
            ctx->builder, LLVMInt8TypeInContext(ctx->context), data_ptr, &i_val, 1, "print.char.ptr"
        );
        LLVMValueRef char_val = LLVMBuildLoad2(ctx->builder, LLVMInt8TypeInContext(ctx->context), char_ptr, "print.char");
        LLVMValueRef char_ext = LLVMBuildZExt(ctx->builder, char_val, LLVMInt32TypeInContext(ctx->context), "print.char.ext");
        LLVMValueRef putchar_args[] = {char_ext};
        LLVMBuildCall2(ctx->builder, putchar_ftype, putchar_fn, putchar_args, 1, "");

        LLVMValueRef i_next = LLVMBuildAdd(
            ctx->builder, i_val, LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false), "print.i.next"
        );
        LLVMBuildStore(ctx->builder, i_next, i_alloca);
        LLVMBuildBr(ctx->builder, loop_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    case AST_NODE_PRINT_BYTE_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand = node->list.children[0];
        LLVMValueRef byte_val = ir_gen_node(ctx, operand);
        if (byte_val == NULL)
            return NULL;

        LLVMTypeRef val_type = LLVMTypeOf(byte_val);
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
            byte_val = LLVMBuildLoad2(ctx->builder, LLVMInt8TypeInContext(ctx->context), byte_val, "pb.val");

        // Extend to i32 for putchar call
        LLVMValueRef char_ext = LLVMBuildZExt(ctx->builder, byte_val, LLVMInt32TypeInContext(ctx->context), "pb.ext");

        // Declare/use putchar
        LLVMTypeRef putchar_param_types[] = {LLVMInt32TypeInContext(ctx->context)};
        LLVMTypeRef putchar_ftype = LLVMFunctionType(
            LLVMInt32TypeInContext(ctx->context), putchar_param_types, 1, false
        );
        LLVMValueRef putchar_fn = LLVMGetNamedFunction(ctx->module, "putchar");
        if (putchar_fn == NULL)
        {
            putchar_fn = LLVMAddFunction(ctx->module, "putchar", putchar_ftype);
            LLVMSetLinkage(putchar_fn, LLVMExternalLinkage);
        }
        LLVMValueRef putchar_args[] = {char_ext};
        LLVMBuildCall2(ctx->builder, putchar_ftype, putchar_fn, putchar_args, 1, "");
        return NULL;
    }

    case AST_NODE_INT_TO_STRING_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand = node->list.children[0];
        LLVMValueRef val = ir_gen_node(ctx, operand);
        if (val == NULL)
            return NULL;

        TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
        if (str_desc == NULL || str_desc->llvm_type == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "int_to_string: string type not found");
            return NULL;
        }
        LLVMTypeRef str_struct_type = str_desc->llvm_type;

        // Load through pointer if needed
        LLVMTypeRef val_type = LLVMTypeOf(val);
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
            val = LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), val, "its.val");

        if (LLVMGetTypeKind(val_type) != LLVMIntegerTypeKind)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "int_to_string requires an integer value");
            return NULL;
        }
        unsigned src_width = LLVMGetIntTypeWidth(val_type);
        LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
        LLVMValueRef i64_val = val;
        if (src_width < 64)
            i64_val = LLVMBuildSExt(ctx->builder, val, i64_type, "its.ext");
        else if (src_width > 64)
            i64_val = LLVMBuildTrunc(ctx->builder, val, i64_type, "its.trunc");

        LLVMValueRef current_func = func_current_function(ctx);
        LLVMValueRef zero = LLVMConstInt(i64_type, 0, false);

        // Sign check and absolute value (as unsigned, safe for INT64_MIN)
        LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i64_val, zero, "its.isneg");
        LLVMValueRef neg_val = LLVMBuildSub(ctx->builder, zero, i64_val, "its.neg");
        LLVMValueRef abs_val = LLVMBuildSelect(ctx->builder, is_neg, neg_val, i64_val, "its.abs");

        // Allocas for loop state
        LLVMValueRef abs_saved = LLVMBuildAlloca(ctx->builder, i64_type, "its.abs.saved");
        LLVMBuildStore(ctx->builder, abs_val, abs_saved);
        LLVMValueRef n_digits_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.ndigits");
        LLVMBuildStore(ctx->builder, zero, n_digits_a);
        LLVMValueRef temp_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.temp");
        LLVMBuildStore(ctx->builder, abs_val, temp_a);

        // --- Count digits (do-while: n_digits++, temp/=10; while temp > 0) ---
        LLVMBasicBlockRef ck_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.ck");
        LLVMBasicBlockRef cb_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.cb");
        LLVMBasicBlockRef cd_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.cd");

        LLVMBuildBr(ctx->builder, ck_bb);
        // Check: run if n_digits == 0 (first iteration) or temp > 0 (subsequent)
        LLVMPositionBuilderAtEnd(ctx->builder, ck_bb);
        LLVMValueRef nd = LLVMBuildLoad2(ctx->builder, i64_type, n_digits_a, "its.nd");
        LLVMValueRef tp = LLVMBuildLoad2(ctx->builder, i64_type, temp_a, "its.tp");
        LLVMValueRef first_iter = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd, zero, "its.first");
        LLVMValueRef still_has = LLVMBuildICmp(ctx->builder, LLVMIntUGT, tp, zero, "its.still");
        LLVMValueRef run_count = LLVMBuildOr(ctx->builder, first_iter, still_has, "its.run");
        LLVMBuildCondBr(ctx->builder, run_count, cb_bb, cd_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cb_bb);
        LLVMValueRef nd_new = LLVMBuildAdd(ctx->builder, nd, LLVMConstInt(i64_type, 1, false), "its.nd+");
        LLVMBuildStore(ctx->builder, nd_new, n_digits_a);
        LLVMValueRef tp_new = LLVMBuildUDiv(ctx->builder, tp, LLVMConstInt(i64_type, 10, false), "its.tp/");
        LLVMBuildStore(ctx->builder, tp_new, temp_a);
        LLVMBuildBr(ctx->builder, ck_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cd_bb);
        LLVMValueRef n_digits = LLVMBuildLoad2(ctx->builder, i64_type, n_digits_a, "its.n");
        LLVMValueRef sign_ext = LLVMBuildZExt(ctx->builder, is_neg, i64_type, "its.sx");
        LLVMValueRef total_len = LLVMBuildAdd(ctx->builder, n_digits, sign_ext, "its.len");

        // Allocate 21-byte buffer, bitcast to i8*
        LLVMValueRef buf_a = LLVMBuildAlloca(ctx->builder, LLVMArrayType(LLVMInt8TypeInContext(ctx->context), 21), "its.buf");
        LLVMValueRef buf_p = LLVMBuildBitCast(ctx->builder, buf_a, LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0), "its.bp");

        // --- Fill digits right-to-left (do-while: pos--, *pos=digit; while remaining>0) ---
        LLVMValueRef rem_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.rem");
        LLVMBuildStore(ctx->builder, LLVMBuildLoad2(ctx->builder, i64_type, abs_saved, "its.abs"), rem_a);
        LLVMValueRef pos_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.pos");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i64_type, 20, false), pos_a);

        LLVMBasicBlockRef fck_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fck");
        LLVMBasicBlockRef fbd_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fbd");
        LLVMBasicBlockRef fdn_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fdn");
        LLVMMoveBasicBlockAfter(fck_bb, cd_bb);
        LLVMMoveBasicBlockAfter(fbd_bb, fck_bb);
        LLVMMoveBasicBlockAfter(fdn_bb, fbd_bb);

        LLVMBuildBr(ctx->builder, fck_bb);
        // Check: run if pos == 20 (first) or remaining > 0 (subsequent)
        LLVMPositionBuilderAtEnd(ctx->builder, fck_bb);
        LLVMValueRef pos_v = LLVMBuildLoad2(ctx->builder, i64_type, pos_a, "its.pv");
        LLVMValueRef rem_v = LLVMBuildLoad2(ctx->builder, i64_type, rem_a, "its.rv");
        LLVMValueRef at_end = LLVMBuildICmp(ctx->builder, LLVMIntEQ, pos_v, LLVMConstInt(i64_type, 20, false), "its.atend");
        LLVMValueRef more_dig = LLVMBuildICmp(ctx->builder, LLVMIntUGT, rem_v, zero, "its.more");
        LLVMValueRef run_fill = LLVMBuildOr(ctx->builder, at_end, more_dig, "its.runfill");
        LLVMBuildCondBr(ctx->builder, run_fill, fbd_bb, fdn_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, fbd_bb);
        LLVMValueRef new_pos = LLVMBuildSub(ctx->builder, pos_v, LLVMConstInt(i64_type, 1, false), "its.p-");
        LLVMBuildStore(ctx->builder, new_pos, pos_a);
        LLVMValueRef digit = LLVMBuildURem(ctx->builder, rem_v, LLVMConstInt(i64_type, 10, false), "its.digit");
        LLVMValueRef ch = LLVMBuildAdd(ctx->builder, digit, LLVMConstInt(i64_type, '0', false), "its.ch");
        LLVMValueRef ch8 = LLVMBuildTrunc(ctx->builder, ch, LLVMInt8TypeInContext(ctx->context), "its.ch8");
        LLVMValueRef cp = LLVMBuildInBoundsGEP2(ctx->builder, LLVMInt8TypeInContext(ctx->context), buf_p, &new_pos, 1, "its.cp");
        LLVMBuildStore(ctx->builder, ch8, cp);
        LLVMValueRef new_rem = LLVMBuildUDiv(ctx->builder, rem_v, LLVMConstInt(i64_type, 10, false), "its.r/");
        LLVMBuildStore(ctx->builder, new_rem, rem_a);
        LLVMBuildBr(ctx->builder, fck_bb);

        // --- Handle sign and build string struct ---
        LLVMPositionBuilderAtEnd(ctx->builder, fdn_bb);
        LLVMValueRef final_pos = LLVMBuildLoad2(ctx->builder, i64_type, pos_a, "its.fp");
        LLVMValueRef neg_pos = LLVMBuildSub(ctx->builder, final_pos, LLVMConstInt(i64_type, 1, false), "its.np");
        LLVMValueRef data_start = LLVMBuildSelect(ctx->builder, is_neg, neg_pos, final_pos, "its.ds");

        LLVMBasicBlockRef sy_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.sy");
        LLVMBasicBlockRef sa_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.sa");
        LLVMMoveBasicBlockAfter(sy_bb, fdn_bb);
        LLVMMoveBasicBlockAfter(sa_bb, sy_bb);
        LLVMBuildCondBr(ctx->builder, is_neg, sy_bb, sa_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, sy_bb);
        LLVMValueRef np = LLVMBuildInBoundsGEP2(ctx->builder, LLVMInt8TypeInContext(ctx->context), buf_p, &neg_pos, 1, "its.np");
        LLVMBuildStore(ctx->builder, LLVMConstInt(LLVMInt8TypeInContext(ctx->context), '-', false), np);
        LLVMBuildBr(ctx->builder, sa_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, sa_bb);
        LLVMValueRef dp = LLVMBuildInBoundsGEP2(ctx->builder, LLVMInt8TypeInContext(ctx->context), buf_p, &data_start, 1, "its.dp");
        LLVMValueRef sv = LLVMGetUndef(str_struct_type);
        sv = LLVMBuildInsertValue(ctx->builder, sv, dp, 0, "its.sd");
        sv = LLVMBuildInsertValue(ctx->builder, sv, total_len, 1, "its.sl");
        return sv;
    }

    case AST_NODE_TYPE_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        LLVMValueRef operand = ir_gen_node(ctx, node->list.children[0]);
        if (operand == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        TypeDescriptor const * operand_type = node->list.children[0]->resolved_type;
        if (operand_type && operand_type->kind == TD_KIND_BASIC && operand_type->as.basic.name
            && strcmp(operand_type->as.basic.name, "any") == 0)
        {
            // Runtime type: extract type_id field (field 1) from the any struct
            LLVMTypeRef any_type = operand_type->llvm_type;
            LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, any_type, "typeof.tmp");
            LLVMBuildStore(ctx->builder, operand, tmp);
            LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
            LLVMValueRef gep[2] = {idx0, idx1};
            LLVMValueRef id_field = LLVMBuildInBoundsGEP2(ctx->builder, any_type, tmp, gep, 2, "typeof.typeid");
            return LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), id_field, "typeof.tid");
        }
        if (operand_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)operand_type->type_id, false);
    }

    case AST_NODE_SIZE_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * type_node = node->list.children[0];
        TypeDescriptor const * td = type_node->resolved_type;
        if (td == NULL || td->llvm_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        uint64_t size = LLVMABISizeOfType(ctx->data_layout, td->llvm_type);
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), size, false);
    }

    case AST_NODE_ALIGN_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * type_node = node->list.children[0];
        TypeDescriptor const * td = type_node->resolved_type;
        if (td == NULL || td->llvm_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false);
        uint32_t align = LLVMABIAlignmentOfType(ctx->data_layout, td->llvm_type);
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), align, false);
    }

    case AST_NODE_OFFSET_OF_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        odin_grammar_node_t * type_node = node->list.children[0];
        odin_grammar_node_t * field_node = node->list.children[1];
        TypeDescriptor const * td = type_node->resolved_type;
        if (td == NULL || td->llvm_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        if (td->kind != TD_KIND_STRUCT && td->kind != TD_KIND_SOA && td->kind != TD_KIND_UNION
            && td->kind != TD_KIND_BIT_FIELD)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        char const * field_name = field_node ? field_node->text : NULL;
        if (field_name == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        field_access_path_t path;
        if (type_descriptor_find_struct_field_path(td, field_name, &path))
        {
            // Walk path to find the final field; compute cumulative offset using LLVM
            uint64_t offset = 0;
            TypeDescriptor const * walk = td;
            for (int pi = 0; pi < path.count; pi++)
            {
                struct_field_t const * f = type_descriptor_get_struct_field(walk, path.indices[pi]);
                if (f == NULL)
                    break;
                if (LLVMGetTypeKind(walk->llvm_type) == LLVMStructTypeKind)
                    offset += LLVMOffsetOfElement(ctx->data_layout, walk->llvm_type, (unsigned)path.indices[pi]);
                walk = f->type_desc;
            }
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), offset, false);
        }
        // For union offset_of always returns 0
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
    }

    case AST_NODE_RAW_DATA_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand = node->list.children[0];
        LLVMValueRef operand_val = ir_gen_node(ctx, operand);
        if (operand_val == NULL)
            return NULL;
        TypeDescriptor const * operand_type = operand->resolved_type;
        if (operand_type == NULL)
            return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
        TypeDescriptor const * elem_type = operand_type->element_type;
        LLVMTypeRef elem_ptr = elem_type ? LLVMPointerType(elem_type->llvm_type, 0)
                                         : LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
        if (operand_type->kind == TD_KIND_ARRAY)
        {
            if (LLVMGetTypeKind(LLVMTypeOf(operand_val)) == LLVMPointerTypeKind)
                return LLVMBuildPointerCast(ctx->builder, operand_val, elem_ptr, "raw_data");
            LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, LLVMTypeOf(operand_val), "raw_data.arr");
            LLVMBuildStore(ctx->builder, operand_val, alloca);
            return LLVMBuildPointerCast(ctx->builder, alloca, elem_ptr, "raw_data");
        }
        // Slice or dynamic array: extract data pointer (field 0)
        LLVMTypeRef val_type = LLVMTypeOf(operand_val);
        LLVMValueRef data_ptr = NULL;
        if (LLVMGetTypeKind(val_type) == LLVMPointerTypeKind)
        {
            LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                                      LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
            data_ptr = LLVMBuildLoad2(
                ctx->builder,
                LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0),
                LLVMBuildInBoundsGEP2(ctx->builder, operand_type->llvm_type, operand_val, indices, 2, "rd.gep"),
                "raw_data"
            );
        }
        else
        {
            data_ptr = LLVMBuildExtractValue(ctx->builder, operand_val, 0, "raw_data");
        }
        if (elem_ptr)
            return LLVMBuildPointerCast(ctx->builder, data_ptr, elem_ptr, "raw_data");
        return data_ptr;
    }

    case AST_NODE_MIN_EXPR:
    case AST_NODE_MAX_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        bool is_min = (node->type == AST_NODE_MIN_EXPR);
        LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
        LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[1]);
        if (lhs == NULL || rhs == NULL)
            return lhs ? lhs : rhs;
        LLVMTypeRef cmp_type = LLVMTypeOf(lhs);
        LLVMTypeKind tk = LLVMGetTypeKind(cmp_type);
        LLVMValueRef cmp;
        if (tk == LLVMIntegerTypeKind)
        {
            cmp = LLVMBuildICmp(ctx->builder, is_min ? LLVMIntSLT : LLVMIntSGT, lhs, rhs, "mm.cmp");
        }
        else if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind)
        {
            cmp = LLVMBuildFCmp(ctx->builder, is_min ? LLVMRealOLT : LLVMRealOGT, lhs, rhs, "mm.cmp");
        }
        else
        {
            return lhs;
        }
        return LLVMBuildSelect(ctx->builder, cmp, lhs, rhs, is_min ? "min" : "max");
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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "make: target type is not a slice, dynamic array, or map");
            return NULL;
        }

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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "make(map): key or value type is NULL");
                return NULL;
            }

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
            {
                ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "make(slice): element type is NULL");
                return NULL;
            }

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
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "new: target type is not a pointer");
            return NULL;
        }
        TypeDescriptor const * pointee_type = ptr_type->pointee;
        if (pointee_type == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "new: pointer has no pointee type");
            return NULL;
        }

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

    case AST_NODE_INCL_EXPR:
    case AST_NODE_EXCL_EXPR:
    {
        if (node->list.count < 2)
            return NULL;
        bool is_incl = (node->type == AST_NODE_INCL_EXPR);
        LLVMValueRef bs_ptr = ir_gen_node(ctx, node->list.children[0]);
        LLVMValueRef elem_val = ir_gen_node(ctx, node->list.children[1]);
        if (bs_ptr == NULL || elem_val == NULL)
            return NULL;
        TypeDescriptor const * ptr_type = node->list.children[0]->resolved_type;
        if (ptr_type == NULL || ptr_type->kind != TD_KIND_POINTER)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "incl/excl: operand is not a pointer");
            return NULL;
        }
        TypeDescriptor const * bs_type = ptr_type->pointee;
        if (bs_type == NULL || bs_type->kind != TD_KIND_BIT_SET)
        {
            ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "incl/excl: pointee is not a bit_set");
            return NULL;
        }
        LLVMTypeRef backing_type = bs_type->llvm_type;
        LLVMValueRef backing = LLVMBuildLoad2(ctx->builder, backing_type, bs_ptr, "bs.backing");
        LLVMValueRef elem_cast = LLVMBuildIntCast(ctx->builder, elem_val, backing_type, "bs.elem");
        LLVMValueRef one = LLVMConstInt(backing_type, 1, false);
        LLVMValueRef mask = LLVMBuildShl(ctx->builder, one, elem_cast, "bs.mask");
        LLVMValueRef result;
        if (is_incl)
            result = LLVMBuildOr(ctx->builder, backing, mask, "bs.incl");
        else
            result = LLVMBuildAnd(ctx->builder, backing, LLVMBuildNot(ctx->builder, mask, "bs.nmask"), "bs.excl");
        LLVMBuildStore(ctx->builder, result, bs_ptr);
        return NULL;
    }

    case AST_NODE_COMPLEX_EXPR:
    case AST_NODE_QUATERNION_EXPR:
    {
        if (node->resolved_type == NULL || node->list.count < 2)
            return NULL;
        LLVMTypeRef struct_type = node->resolved_type->llvm_type;
        LLVMValueRef result = LLVMGetUndef(struct_type);
        for (size_t i = 0; i < node->list.count; i++)
        {
            LLVMValueRef val = ir_gen_node(ctx, node->list.children[i]);
            if (val == NULL)
                return NULL;
            result = LLVMBuildInsertValue(ctx->builder, result, val, (unsigned)i, "complex.field");
        }
        return result;
    }

    case AST_NODE_NIL:
    case AST_NODE_NONE:
        return ir_gen_nil(ctx, node);

    case AST_NODE_IDENTIFIER:
        return ir_gen_identifier(ctx, node);

    case AST_NODE_CONTEXT_EXPR:
    {
        symbol_t * ctx_sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), "context");
        if (ctx_sym && ctx_sym->value.value)
        {
            return ctx_sym->value.value;
        }
        ir_gen_error_collection_add(&ctx->errors, ctx->file_path, node, "'context' not available here");
        return NULL;
    }

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
        if (func_current_function(ctx) != NULL)
            return ir_gen_nested_procedure_decl(ctx, node);
        return ir_gen_top_level_decl(ctx, node);

    case AST_NODE_DIRECTIVE_WITH_ARGS:
    case AST_NODE_DIRECTIVE:
        return NULL;

    case AST_NODE_WHERE_CLAUSE:
        return NULL;

    case AST_NODE_FOREIGN_BLOCK:
    {
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_CONSTANT_DECL)
                ir_gen_top_level_decl(ctx, child);
            else if (child->type == AST_NODE_FOREIGN_IMPORT)
                ir_gen_collect_foreign_import(ctx, child);
        }
        return NULL;
    }

    case AST_NODE_FOREIGN_IMPORT:
        ir_gen_collect_foreign_import(ctx, node);
        return NULL;

    case AST_NODE_USING_DECL:
    {
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_CONSTANT_DECL)
                ir_gen_top_level_decl(ctx, child);
            else if (child->type == AST_NODE_VARIABLE_DECL)
                ir_gen_top_level_variable(ctx, child);
        }
        return NULL;
    }

    default:
        return NULL;
    }

#pragma GCC diagnostic pop
}

// --- When declaration helpers ---

static int
ir_gen_evaluate_constant_bool(odin_grammar_node_t * node)
{
    if (node == NULL)
        return -1;
    // Unwrap through expression chain to reach a node type we can evaluate
    // Most expression wrappers store the inner expression in child[0];
    // POSTFIX_EXPRESSION has [operand, postfix_ops] so use child[0] too.
    while (1)
    {
        int can_eval = 0;
        switch (node->type)
        {
        case AST_NODE_BOOL_TRUE:
        case AST_NODE_BOOL_FALSE:
        case AST_NODE_INTEGER_VALUE:
        case AST_NODE_UNARY_EXPRESSION:
        case AST_NODE_COMP_EXPRESSION:
            can_eval = 1;
            break;
        default:
            break;
        }
        if (can_eval)
            break;
        if ((node->type == AST_NODE_POSTFIX_EXPRESSION || node->list.count == 1) && node->list.children[0])
            node = node->list.children[0];
        else
            break;
    }
    switch (node->type)
    {
    case AST_NODE_BOOL_TRUE:
        return 1;
    case AST_NODE_BOOL_FALSE:
        return 0;
    case AST_NODE_INTEGER_VALUE:
    {
        if (node->text == NULL)
            return -1;
        char * end = NULL;
        long long val = strtoll(node->text, &end, 0);
        if (end == node->text)
            return -1;
        return (val != 0) ? 1 : 0;
    }
    default:
        return -1;
    }
}

static void
ir_gen_when_body(IrGenContext * ctx, odin_grammar_node_t * body)
{
    for (size_t m = 0; m < body->list.count; m++)
    {
        odin_grammar_node_t * inner = body->list.children[m];
        if (inner == NULL)
            continue;
        if (inner->type == AST_NODE_CONSTANT_DECL)
            ir_gen_top_level_decl(ctx, inner);
        else if (inner->type == AST_NODE_VARIABLE_DECL)
            ir_gen_top_level_variable(ctx, inner);
    }
}

// --- Main entry point ---

static void
ir_gen_process_top_level_decl(IrGenContext * ctx, odin_grammar_node_t * top_decl)
{
    if (top_decl == NULL)
        return;
    if (top_decl->type == AST_NODE_CONSTANT_DECL)
        ir_gen_top_level_decl(ctx, top_decl);
    else if (top_decl->type == AST_NODE_VARIABLE_DECL)
        ir_gen_top_level_variable(ctx, top_decl);
    else if (top_decl->type == AST_NODE_FOREIGN_BLOCK)
    {
        for (size_t k = 0; k < top_decl->list.count; k++)
        {
            odin_grammar_node_t * fb_child = top_decl->list.children[k];
            if (fb_child == NULL || fb_child->type != AST_NODE_CONSTANT_DECL)
                continue;
            ir_gen_top_level_decl(ctx, fb_child);
        }
    }
    else if (top_decl->type == AST_NODE_FOREIGN_IMPORT)
        ir_gen_collect_foreign_import(ctx, top_decl);
    else if (top_decl->type == AST_NODE_USING_DECL)
    {
        for (size_t k = 0; k < top_decl->list.count; k++)
        {
            odin_grammar_node_t * inner = top_decl->list.children[k];
            if (inner == NULL)
                continue;
            if (inner->type == AST_NODE_CONSTANT_DECL)
                ir_gen_top_level_decl(ctx, inner);
            else if (inner->type == AST_NODE_VARIABLE_DECL)
                ir_gen_top_level_variable(ctx, inner);
        }
    }
    else if (top_decl->type == AST_NODE_WHEN_DECL)
    {
        size_t k = 0;
        bool matched = false;
        while (k < top_decl->list.count)
        {
            odin_grammar_node_t * wc = top_decl->list.children[k];
            if (wc == NULL)
            {
                k++;
                continue;
            }
            if (wc->type == AST_NODE_WHEN_BODY)
            {
                if (!matched)
                    ir_gen_when_body(ctx, wc);
                break;
            }
            int cond = ir_gen_evaluate_constant_bool(wc);
            k++;
            if (cond == 1 && !matched)
            {
                matched = true;
                if (k < top_decl->list.count)
                {
                    odin_grammar_node_t * body = top_decl->list.children[k];
                    if (body && body->type == AST_NODE_WHEN_BODY)
                        ir_gen_when_body(ctx, body);
                }
            }
            k++;
        }
    }
}

static void
ir_gen_process_ast(IrGenContext * ctx, odin_grammar_node_t * ast)
{
    if (ast == NULL)
        return;
    for (size_t i = 0; i < ast->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = ast->list.children[i];
        if (ext_decl == NULL || ext_decl->type != AST_NODE_EXTERNAL_DECLARATIONS)
            continue;
        for (size_t j = 0; j < ext_decl->list.count; j++)
            ir_gen_process_top_level_decl(ctx, ext_decl->list.children[j]);
    }
}

static void
import_using_copy_symbol(void * value, void * user_data)
{
    symbol_t * sym = (symbol_t *)value;
    scope_t * target_scope = (scope_t *)user_data;
    if (sym == NULL || sym->name == NULL || target_scope == NULL)
        return;
    scope_add_symbol(target_scope, sym->name, sym->value);
}

bool
ir_generate(IrGenContext * ctx, odin_grammar_node_t * ast)
{
    if (ctx == NULL || ast == NULL)
        return false;

    // Generate code for imported packages first
    for (int i = 0; i < ctx->import_count; i++)
    {
        ImportedPackage * pkg = ctx->imports[i];
        if (pkg == NULL || pkg->ast == NULL || pkg->codegen_done)
            continue;

        int saved_count = ctx->gen_ctx->count;
        if (pkg->package_scope)
            ctx->gen_ctx->scopes[ctx->gen_ctx->count++] = pkg->package_scope;

        ir_gen_process_ast(ctx, pkg->ast);

        ctx->gen_ctx->count = saved_count;
        pkg->codegen_done = true;
    }

    // Re-copy symbols for 'import using' packages (codegen now has LLVM values)
    scope_t * current = generator_current_scope(ctx->gen_ctx);
    for (int i = 0; i < ctx->import_count; i++)
    {
        ImportedPackage * pkg = ctx->imports[i];
        if (pkg == NULL || !pkg->is_using || pkg->package_scope == NULL)
            continue;
        generic_hash_table_iterate(pkg->package_scope->symbols.by_name, import_using_copy_symbol, current);
    }

    // Generate code for the main AST
    ir_gen_process_ast(ctx, ast);

    // Emit foreign library metadata
    // !llvm.dependent.libraries expects direct MDString operands: !{!"lib1", !"lib2"}
    for (int fi = 0; fi < ctx->foreign_library_count; fi++)
    {
        LLVMValueRef lib_md = LLVMMDStringInContext(
            ctx->context, ctx->foreign_libraries[fi], (unsigned)strlen(ctx->foreign_libraries[fi])
        );
        LLVMAddNamedMetadataOperand(ctx->module, "llvm.dependent.libraries", lib_md);
    }

    // Phase 5: Generate entry point wrapper for Odin main with hidden context param
    LLVMValueRef odin_main = LLVMGetNamedFunction(ctx->module, "main");
    if (odin_main != NULL && LLVMCountParams(odin_main) > 0)
    {
        LLVMSetValueName(odin_main, "__odin_main");
        LLVMSetLinkage(odin_main, LLVMPrivateLinkage);

        LLVMTypeRef i32t = LLVMInt32TypeInContext(ctx->context);
        LLVMTypeRef main_type = LLVMFunctionType(i32t, NULL, 0, false);
        LLVMValueRef c_main = LLVMAddFunction(ctx->module, "main", main_type);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, c_main, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        TypeDescriptor const * ctx_type = type_descriptor_get_context_type(ctx->type_registry);
        LLVMValueRef context_ptr;
        if (ctx_type)
        {
            LLVMValueRef ctx_alloca = LLVMBuildAlloca(ctx->builder, ctx_type->llvm_type, "context");
            LLVMBuildStore(ctx->builder, LLVMConstNull(ctx_type->llvm_type), ctx_alloca);
            context_ptr = ctx_alloca;
        }
        else
        {
            context_ptr = LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
        }

        LLVMTypeRef odin_main_func_type = LLVMGlobalGetValueType(odin_main);
        LLVMValueRef odin_main_args[] = {context_ptr};
        LLVMValueRef result = LLVMBuildCall2(ctx->builder, odin_main_func_type, odin_main, odin_main_args, 1, "");

        if (LLVMGetTypeKind(LLVMGetReturnType(odin_main_func_type)) == LLVMVoidTypeKind)
        {
            LLVMBuildRet(ctx->builder, LLVMConstInt(i32t, 0, false));
        }
        else
        {
            LLVMValueRef ret_val = LLVMBuildTrunc(ctx->builder, result, i32t, "");
            LLVMBuildRet(ctx->builder, ret_val);
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
