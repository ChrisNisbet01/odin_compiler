#include "llvm_ir_generator.h"

#include "ast_metadata.h"
#include "ast_utils.h"
#include "ir_utils.h"
#include "ir_intrinsic.h"
#include "ir_gen_var_decl.h"
#include "ir_gen_statement.h"
#include "ir_gen_operator.h"
#include "ir_gen_postfix.h"
#include "ir_gen_assign.h"
#include "operator_kind.h"
#include "scope.h"
#include "symbols.h"
#include "typed_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// --- Forward declarations ---
LLVMValueRef ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_lvalue(IrGenContext * ctx, odin_grammar_node_t * node);
static LLVMValueRef ir_gen_nested_procedure_decl(IrGenContext * ctx, odin_grammar_node_t * node);
bool ir_gen_node_contains_auto_cast(odin_grammar_node_t * node);
LLVMValueRef ir_gen_emit_bounds_check(IrGenContext * ctx, LLVMValueRef index_val,
                                              LLVMValueRef len_val, odin_grammar_node_t * node);
LLVMValueRef ir_gen_map_subscript(IrGenContext * ctx, LLVMValueRef map_val,
                                          TypeDescriptor const * map_type,
                                          LLVMValueRef index_val,
                                          TypeDescriptor const ** out_val_type,
                                          bool is_lvalue, char const * prefix);

bool ir_gen_resolve_aggregate_field(
    IrGenContext * ctx,
    TypeDescriptor const * agg_type,
    char const * field_name,
    LLVMTypeRef * out_struct_type,
    int * out_field_idx,
    TypeDescriptor const ** out_field_type,
    char const ** out_error_name
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
    ctx->bounds_checking_enabled = true;

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

void
func_push(IrGenContext * ctx, LLVMValueRef func, TypeDescriptor const * return_type)
{
    if (ctx->func_depth < MAX_FUNC_DEPTH)
    {
        ctx->func_stack[ctx->func_depth].function = func;
        ctx->func_stack[ctx->func_depth].return_type = return_type;
        ctx->func_depth++;
    }
}

void
func_pop(IrGenContext * ctx)
{
    if (ctx->func_depth > 0)
        ctx->func_depth--;
}

LLVMValueRef
func_current_function(IrGenContext * ctx)
{
    return ctx->func_depth > 0 ? ctx->func_stack[ctx->func_depth - 1].function : NULL;
}

void
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

// --- Centralized type coercion ---

LLVMValueRef
coerce_value_to_type(IrGenContext * ctx, LLVMValueRef value, LLVMTypeRef target_type,
                     bool src_is_unsigned, char const * name_hint)
{
    if (value == NULL || target_type == NULL)
        return value;

    LLVMTypeRef src_type = LLVMTypeOf(value);
    if (src_type == target_type)
        return value;

    LLVMTypeKind src_kind = LLVMGetTypeKind(src_type);
    LLVMTypeKind dst_kind = LLVMGetTypeKind(target_type);

    // Integer to Integer (trunc, zext, sext)
    if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMIntegerTypeKind)
    {
        unsigned src_w = LLVMGetIntTypeWidth(src_type);
        unsigned dst_w = LLVMGetIntTypeWidth(target_type);
        if (dst_w < src_w)
            return LLVMBuildIntCast2(ctx->builder, value, target_type, false, name_hint ? name_hint : "trunc");
        else if (dst_w > src_w)
        {
            if (src_is_unsigned)
                return LLVMBuildIntCast2(ctx->builder, value, target_type, false, name_hint ? name_hint : "zext");
            else
                return LLVMBuildIntCast2(ctx->builder, value, target_type, true,  name_hint ? name_hint : "sext");
        }
        return value;
    }

    // Float to Float (fptrunc, fpext)
    if ((src_kind == LLVMHalfTypeKind || src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind)
        && (dst_kind == LLVMHalfTypeKind || dst_kind == LLVMFloatTypeKind || dst_kind == LLVMDoubleTypeKind))
    {
        unsigned src_w = (src_kind == LLVMDoubleTypeKind) ? 64 : (src_kind == LLVMFloatTypeKind ? 32 : 16);
        unsigned dst_w = (dst_kind == LLVMDoubleTypeKind) ? 64 : (dst_kind == LLVMFloatTypeKind ? 32 : 16);
        if (dst_w < src_w)
            return LLVMBuildFPTrunc(ctx->builder, value, target_type, name_hint ? name_hint : "fptrunc");
        else if (dst_w > src_w)
            return LLVMBuildFPExt(ctx->builder, value, target_type, name_hint ? name_hint : "fpext");
        return value;
    }

    // Integer to Float (sitofp, uitofp)
    if (src_kind == LLVMIntegerTypeKind
        && (dst_kind == LLVMHalfTypeKind || dst_kind == LLVMFloatTypeKind || dst_kind == LLVMDoubleTypeKind))
    {
        if (src_is_unsigned)
            return LLVMBuildUIToFP(ctx->builder, value, target_type, name_hint ? name_hint : "uitofp");
        else
            return LLVMBuildSIToFP(ctx->builder, value, target_type, name_hint ? name_hint : "sitofp");
    }

    // Float to Integer (fptosi, fptoui)
    if ((src_kind == LLVMHalfTypeKind || src_kind == LLVMFloatTypeKind || src_kind == LLVMDoubleTypeKind)
        && dst_kind == LLVMIntegerTypeKind)
    {
        if (src_is_unsigned)
            return LLVMBuildFPToUI(ctx->builder, value, target_type, name_hint ? name_hint : "fptoui");
        else
            return LLVMBuildFPToSI(ctx->builder, value, target_type, name_hint ? name_hint : "fptosi");
    }

    // Pointer to Integer
    if (src_kind == LLVMPointerTypeKind && dst_kind == LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(ctx->builder, value, target_type, name_hint ? name_hint : "ptrtoint");

    // Integer to Pointer
    if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(ctx->builder, value, target_type, name_hint ? name_hint : "inttoptr");

    return value;
}

// --- Auto-cast helper (delegates to centralized coercion) ---

static LLVMValueRef
ir_gen_auto_cast_value(IrGenContext * ctx, LLVMValueRef src_val, LLVMTypeRef target_type)
{
    if (src_val == NULL || target_type == NULL)
        return src_val;
    LLVMTypeRef src_llvm_type = LLVMTypeOf(src_val);
    if (src_llvm_type == target_type)
        return src_val;
    // Auto-cast always treats source as signed, delegates to centralized coercion
    return coerce_value_to_type(ctx, src_val, target_type, false, "auto");
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
    unsigned long long val = parse_odin_unsigned(node->text, &endptr, 0);
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
        LLVMValueRef existing = LLVMGetNamedFunction(ctx->module, node->text);
        if (existing)
            sym->value.value = existing;
        else
            sym->value.value = LLVMAddFunction(
                ctx->module, node->text, sym->value.type_info->proc_metadata.func_type
            );
    }

    if (sym->value.is_lvalue)
    {
        if (sym->value.value == NULL)
        {
            // Symbol is an lvalue with no backing storage (e.g., type alias used in expression context)
            return NULL;
        }
        // Don't load composite types — the pointer is needed for
        // GEP/subscript/member access
        if (sym->value.type_info
            && (sym->value.type_info->kind == TD_KIND_ARRAY || sym->value.type_info->kind == TD_KIND_SLICE
                || sym->value.type_info->kind == TD_KIND_STRUCT || sym->value.type_info->kind == TD_KIND_SOA
                || sym->value.type_info->kind == TD_KIND_DYNAMIC_ARRAY                 || sym->value.type_info->kind == TD_KIND_MAP
                || sym->value.type_info->kind == TD_KIND_BIT_FIELD || sym->value.type_info->kind == TD_KIND_UNION
                || sym->value.type_info->kind == TD_KIND_MAYBE
                || sym->value.type_info->kind == TD_KIND_MULTI_POINTER
                || sym->value.type_info->kind == TD_KIND_VECTOR))
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

// --- Helper: string global construction ---

// Builds a private unnamed_addr constant global array of i8 from the given bytes,
// then creates a {i8*, i64} string struct pointing to it.
static LLVMValueRef
make_string_global(IrGenContext * ctx, unsigned char const * bytes, size_t byte_count)
{
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    size_t arr_len = byte_count + 1;
    LLVMValueRef * elements = malloc(arr_len * sizeof(LLVMValueRef));
    if (elements == NULL)
        return NULL;
    for (size_t i = 0; i < byte_count; i++)
        elements[i] = LLVMConstInt(i8_type, bytes[i], false);
    elements[byte_count] = LLVMConstInt(i8_type, 0, false);

    LLVMTypeRef arr_type = LLVMArrayType(i8_type, arr_len);
    LLVMValueRef arr_const = LLVMConstArray(i8_type, elements, arr_len);
    free(elements);

    LLVMValueRef global = LLVMAddGlobal(ctx->module, arr_type, ".str");
    LLVMSetInitializer(global, arr_const);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
    LLVMSetGlobalConstant(global, true);

    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
    LLVMValueRef indices[] = {zero, zero};
    LLVMValueRef ptr = LLVMConstInBoundsGEP2(arr_type, global, indices, 2);

    TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
    LLVMTypeRef str_type = str_desc ? str_desc->llvm_type : LLVMStructType(NULL, 0, false);

    LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)byte_count, false);
    LLVMValueRef struct_vals[] = {ptr, len_val};
    return LLVMConstNamedStruct(str_type, struct_vals, 2);
}

static unsigned char
hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
    return 0;
}

// Processes an escape sequence starting at content[i] where content[i] == '\\'.
// Sets *byte_value to the decoded byte and *consumed to number of input chars consumed.
static void
parse_escape_sequence(char const * content, size_t i, size_t content_len, unsigned char * byte_value, size_t * consumed)
{
    if (i + 1 >= content_len)
    {
        *byte_value = 0x5C;
        *consumed = 1;
        return;
    }
    switch (content[i + 1])
    {
    case 'a':  *byte_value = 0x07; *consumed = 2; return;
    case 'b':  *byte_value = 0x08; *consumed = 2; return;
    case 'e':  *byte_value = 0x1B; *consumed = 2; return;
    case 'f':  *byte_value = 0x0C; *consumed = 2; return;
    case 'n':  *byte_value = 0x0A; *consumed = 2; return;
    case 'r':  *byte_value = 0x0D; *consumed = 2; return;
    case 't':  *byte_value = 0x09; *consumed = 2; return;
    case 'v':  *byte_value = 0x0B; *consumed = 2; return;
    case '\\': *byte_value = 0x5C; *consumed = 2; return;
    case '\'': *byte_value = 0x27; *consumed = 2; return;
    case '"':  *byte_value = 0x22; *consumed = 2; return;
    case '0':  *byte_value = 0x00; *consumed = 2; return;
    case 'x':
        if (i + 3 < content_len
            && ((content[i + 2] >= '0' && content[i + 2] <= '9')
             || (content[i + 2] >= 'a' && content[i + 2] <= 'f')
             || (content[i + 2] >= 'A' && content[i + 2] <= 'F'))
            && ((content[i + 3] >= '0' && content[i + 3] <= '9')
             || (content[i + 3] >= 'a' && content[i + 3] <= 'f')
             || (content[i + 3] >= 'A' && content[i + 3] <= 'F')))
        {
            *byte_value = (hex_char_to_int(content[i + 2]) << 4) | hex_char_to_int(content[i + 3]);
            *consumed = 4;
            return;
        }
        *byte_value = 0x5C;
        *consumed = 1;
        return;
    default:
        *byte_value = 0x5C;
        *consumed = 1;
        return;
    }
}

static LLVMValueRef
ir_gen_string_literal(IrGenContext * ctx, odin_grammar_node_t * node, bool process_escapes)
{
    if (node->text == NULL)
        return LLVMConstNull(LLVMStructType(NULL, 0, false));

    char const * text = node->text;
    size_t text_len = strlen(text);

    char const * content = text;
    size_t content_len = text_len;
    if (text_len >= 2 && (text[0] == '"' || text[0] == '`'))
    {
        content = text + 1;
        content_len = text_len - 2;
    }

    if (!process_escapes)
    {
        unsigned char * buf = malloc(content_len * sizeof(unsigned char));
        if (buf == NULL)
            return NULL;
        for (size_t i = 0; i < content_len; i++)
            buf[i] = (unsigned char)content[i];
        LLVMValueRef result = make_string_global(ctx, buf, content_len);
        free(buf);
        return result;
    }

    size_t escaped_len = 0;
    for (size_t i = 0; i < content_len; )
    {
        if (content[i] == '\\')
        {
            unsigned char byte_val;
            size_t consumed;
            parse_escape_sequence(content, i, content_len, &byte_val, &consumed);
            escaped_len++;
            i += consumed;
        }
        else
        {
            escaped_len++;
            i++;
        }
    }

    unsigned char * buf = malloc(escaped_len * sizeof(unsigned char));
    if (buf == NULL)
        return NULL;
    size_t out_idx = 0;
    for (size_t i = 0; i < content_len; )
    {
        if (content[i] == '\\')
        {
            unsigned char byte_val;
            size_t consumed;
            parse_escape_sequence(content, i, content_len, &byte_val, &consumed);
            buf[out_idx++] = byte_val;
            i += consumed;
        }
        else
        {
            buf[out_idx++] = (unsigned char)content[i];
            i++;
        }
    }

    LLVMValueRef result = make_string_global(ctx, buf, escaped_len);
    free(buf);
    return result;
}

static LLVMValueRef
ir_gen_rune_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL)
        return NULL;
    char const * text = node->text;
    size_t text_len = strlen(text);
    // Expect format: 'X' or '\Y'
    if (text_len < 3 || text[0] != '\'' || text[text_len - 1] != '\'')
        return NULL;
    char const * content = text + 1;
    size_t content_len = text_len - 2;
    unsigned char val = 0;
    if (content_len >= 2 && content[0] == '\\')
    {
        size_t consumed = 0;
        parse_escape_sequence(content, 0, content_len, &val, &consumed);
    }
    else if (content_len >= 1)
    {
        val = (unsigned char)content[0];
    }
    return LLVMConstInt(LLVMInt32TypeInContext(ctx->context), val, false);
}

static LLVMValueRef
ir_gen_nil(IrGenContext * ctx, odin_grammar_node_t * node)
{
    (void)node;
    return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
}

// Binary + unary expression codegen moved to ir_gen_operator.c

// --- Enum type codegen ---

void
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

// Defer helpers, statement codegen, and control flow codegen moved to ir_gen_statement.c

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



// --- Aggregate field resolution (string, slice, dynamic_array) ---

bool
ir_gen_resolve_aggregate_field(
    IrGenContext * ctx,
    TypeDescriptor const * agg_type,
    char const * field_name,
    LLVMTypeRef * out_struct_type,
    int * out_field_idx,
    TypeDescriptor const ** out_field_type,
    char const ** out_error_name
)
{
    if (agg_type->kind == TD_KIND_BASIC && agg_type->as.basic.name != NULL
        && strcmp(agg_type->as.basic.name, "string") == 0)
    {
        *out_struct_type = agg_type->llvm_type;
        if (strcmp(field_name, "len") == 0)
        {
            *out_field_idx = 1;
            *out_field_type = get_basic_type_by_name(ctx->type_registry, "int");
            return true;
        }
        if (strcmp(field_name, "data") == 0)
        {
            *out_field_idx = 0;
            *out_field_type = get_basic_type_by_name(ctx->type_registry, "u8");
            return true;
        }
        if (out_error_name) *out_error_name = "string";
        return false;
    }

    if (agg_type->kind == TD_KIND_SLICE)
    {
        *out_struct_type = agg_type->llvm_type;
        if (strcmp(field_name, "len") == 0)
        {
            *out_field_idx = 1;
            *out_field_type = get_basic_type_by_name(ctx->type_registry, "int");
            return true;
        }
        if (strcmp(field_name, "data") == 0)
        {
            *out_field_idx = 0;
            *out_field_type = get_or_create_pointer_type(ctx->type_registry, agg_type->element_type);
            return true;
        }
        if (out_error_name) *out_error_name = "slice";
        return false;
    }

    if (agg_type->kind == TD_KIND_DYNAMIC_ARRAY)
    {
        *out_struct_type = agg_type->llvm_type;
        if (strcmp(field_name, "len") == 0)
        {
            *out_field_idx = 1;
            *out_field_type = get_basic_type_by_name(ctx->type_registry, "int");
            return true;
        }
        if (strcmp(field_name, "cap") == 0)
        {
            *out_field_idx = 2;
            *out_field_type = get_basic_type_by_name(ctx->type_registry, "int");
            return true;
        }
        if (strcmp(field_name, "data") == 0)
        {
            *out_field_idx = 0;
            *out_field_type = get_or_create_pointer_type(ctx->type_registry, agg_type->element_type);
            return true;
        }
        if (out_error_name) *out_error_name = "dynamic array";
        return false;
    }

    return false;
}

// --- Bounds checking support ---

LLVMValueRef
ir_gen_emit_bounds_check(IrGenContext * ctx, LLVMValueRef index_val,
                         LLVMValueRef len_val, odin_grammar_node_t * node)
{
    if (!ctx->bounds_checking_enabled)
        return index_val;

    (void)node;

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    // Extend index to i64 if needed
    LLVMTypeRef idx_type = LLVMTypeOf(index_val);
    if (LLVMGetTypeKind(idx_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(idx_type) != 64)
        index_val = LLVMBuildIntCast2(ctx->builder, index_val, i64_type, false, "bc.idx");

    // Check: index >= len → out of bounds
    LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntUGE, index_val, len_val, "oob");

    LLVMBasicBlockRef current_bb = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef func = LLVMGetBasicBlockParent(current_bb);

    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "bc.cont");
    LLVMBasicBlockRef trap_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "bc.trap");

    LLVMBuildCondBr(ctx->builder, cond, trap_bb, cont_bb);

    // Emit trap block
    LLVMPositionBuilderAtEnd(ctx->builder, trap_bb);
    LLVMValueRef trap_fn = LLVMGetNamedFunction(ctx->module, "llvm.trap");
    if (!trap_fn)
    {
        LLVMTypeRef trap_ft = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), NULL, 0, false);
        trap_fn = LLVMAddFunction(ctx->module, "llvm.trap", trap_ft);
    }
    LLVMBuildCall2(ctx->builder, LLVMGlobalGetValueType(trap_fn), trap_fn, NULL, 0, "");
    LLVMBuildUnreachable(ctx->builder);

    // Continue in cont_bb
    LLVMPositionBuilderAtEnd(ctx->builder, cont_bb);

    return index_val;
}

// --- Top-level declaration codegen ---

static void
ir_gen_os_args_init(IrGenContext * ctx)
{
    // Find the os package in imports
    ImportedPackage * os_pkg = NULL;
    for (int i = 0; i < ctx->import_count; i++)
    {
        if (ctx->imports[i] && strcmp(ctx->imports[i]->package_name, "os") == 0)
        {
            os_pkg = ctx->imports[i];
            break;
        }
    }
    if (os_pkg == NULL || os_pkg->package_scope == NULL)
        return;

    symbol_t * args_sym = scope_find_symbol_entry(os_pkg->package_scope, "args");
    if (args_sym == NULL || args_sym->value.value == NULL)
        return;

    LLVMValueRef os_args_global = args_sym->value.value;
    if (!LLVMIsAGlobalVariable(os_args_global))
        return;

    LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);

    // 1. Load argc/argv from globals
    LLVMValueRef argc_val = LLVMBuildLoad2(ctx->builder, i64t, ctx->odin_argc_global, "os.args.argc");
    LLVMValueRef argv_val = LLVMBuildLoad2(ctx->builder, LLVMPointerType(i8ptr, 0), ctx->odin_argv_global, "os.args.argv");

    // 2. Get string type = {i8*, i64}
    TypeDescriptor const * str_td = get_basic_type_by_name(ctx->type_registry, "string");
    if (str_td == NULL)
        return;
    LLVMTypeRef str_type = str_td->llvm_type;

    // 3. Get slice type for []string
    TypeDescriptor const * slice_td = get_or_create_slice_type(ctx->type_registry, str_td);
    if (slice_td == NULL)
        return;
    LLVMTypeRef slice_type = slice_td->llvm_type;

    // 4. Allocate backing array: malloc(argc * sizeof(string))
    LLVMValueRef elem_size = LLVMConstInt(
        i64t,
        (long long)LLVMABISizeOfType(ctx->data_layout, str_type),
        false
    );
    LLVMValueRef total_size = LLVMBuildMul(ctx->builder, argc_val, elem_size, "os.args.totalsize");
    LLVMValueRef backing_raw = ir_gen_call_malloc(ctx, total_size);
    LLVMValueRef backing = LLVMBuildPointerCast(ctx->builder, backing_raw, LLVMPointerType(str_type, 0), "os.args.backing");

    // 5. Set up loop: for (i64 i = 0; i < argc; i++) { ... }
    LLVMValueRef func = func_current_function(ctx);
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "os.args.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "os.args.body");
    LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "os.args.inc");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func, "os.args.end");

    // Init: i = 0
    LLVMValueRef i_alloca = LLVMBuildAlloca(ctx->builder, i64t, "os.args.i");
    LLVMBuildStore(ctx->builder, LLVMConstNull(i64t), i_alloca);
    LLVMBuildBr(ctx->builder, cond_bb);

    // Cond: i < argc
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef i_val = LLVMBuildLoad2(ctx->builder, i64t, i_alloca, "os.args.i.val");
    LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i_val, argc_val, "os.args.cond");
    LLVMBuildCondBr(ctx->builder, cond, body_bb, end_bb);

    // Body: argv[i] → string{data, len}
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    LLVMValueRef argv_i_ptr = LLVMBuildGEP2(ctx->builder, i8ptr, argv_val, &i_val, 1, "os.args.argv.i");
    LLVMValueRef char_ptr = LLVMBuildLoad2(ctx->builder, i8ptr, argv_i_ptr, "os.args.char.ptr");
    LLVMValueRef str_len = ir_gen_call_strlen(ctx, char_ptr);
    LLVMValueRef str_val = LLVMBuildInsertValue(ctx->builder, LLVMGetUndef(str_type), char_ptr, 0, "os.args.str.0");
    str_val = LLVMBuildInsertValue(ctx->builder, str_val, str_len, 1, "os.args.str.1");
    LLVMValueRef backing_i_ptr = LLVMBuildGEP2(ctx->builder, str_type, backing, &i_val, 1, "os.args.backing.i");
    LLVMBuildStore(ctx->builder, str_val, backing_i_ptr);
    LLVMBuildBr(ctx->builder, inc_bb);

    // Inc: i++
    LLVMPositionBuilderAtEnd(ctx->builder, inc_bb);
    LLVMValueRef one = LLVMConstInt(i64t, 1, false);
    LLVMValueRef i_next = LLVMBuildAdd(ctx->builder, i_val, one, "os.args.i.next");
    LLVMBuildStore(ctx->builder, i_next, i_alloca);
    LLVMBuildBr(ctx->builder, cond_bb);

    // End: build []string slice and store in os.args global
    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    LLVMValueRef slice_val = LLVMBuildInsertValue(ctx->builder, LLVMGetUndef(slice_type), backing, 0, "os.args.slice.0");
    slice_val = LLVMBuildInsertValue(ctx->builder, slice_val, argc_val, 1, "os.args.slice.1");
    LLVMBuildStore(ctx->builder, slice_val, os_args_global);
}

static LLVMValueRef
ir_gen_top_level_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;

    odin_grammar_node_t * name_node = node_find_child(node, AST_NODE_IDENTIFIER);
    odin_grammar_node_t * value_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && child != name_node && child->type != AST_NODE_ATTRIBUTE)
        {
            value_node = child;
            break;
        }
    }

    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    if (value_node->type == AST_NODE_PROCEDURE_DEFINITION)
    {
        TypeDescriptor const * proc_type = value_node->resolved_type;
        if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
            return NULL;

        ProcDeclAttributes * attrs = (ProcDeclAttributes *)node->metadata;
        char const * func_name = (attrs && attrs->link_name) ? attrs->link_name : name_node->text;

        LLVMValueRef func = LLVMGetNamedFunction(ctx->module, func_name);
        if (func == NULL)
            func = LLVMAddFunction(ctx->module, func_name, proc_type->proc_metadata.func_type);

        odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);

        TypedValue tv = create_typed_value(func, proc_type, false);
        generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

        bool is_builtin = (attrs && attrs->is_builtin);

        if (body_node || is_builtin)
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

            if (strcmp(func_name, "main") == 0)
            {
                ir_gen_os_args_init(ctx);
            }

            if (body_node)
                ir_gen_node(ctx, body_node);
            else
                ir_gen_runtime_intrinsic_body(ctx, func_name, proc_type);

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

        bool has_init = false;
        if (vi == 0)
        {
            if (node->list.count >= 3)
            {
                odin_grammar_node_t * init_node = node->list.children[2];
                if (init_node)
                {
                    LLVMValueRef init_val = ir_gen_node(ctx, init_node);
                    if (init_val)
                    {
                        LLVMSetInitializer(global, init_val);
                        has_init = true;
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
                        has_init = true;
                    }
                }
            }
        }
        if (!has_init)
        {
            LLVMSetInitializer(global, LLVMConstNull(llvm_type));
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
    odin_grammar_node_t * name_node = node_find_child(node, AST_NODE_IDENTIFIER);
    odin_grammar_node_t * value_node = NULL;
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && child != name_node && child->type != AST_NODE_ATTRIBUTE)
        {
            value_node = child;
            break;
        }
    }
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    if (value_node == NULL || value_node->type != AST_NODE_PROCEDURE_DEFINITION)
        return NULL;

    TypeDescriptor const * proc_type = value_node->resolved_type;
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
        return NULL;

    // Save outer insertion block
    LLVMBasicBlockRef outer_block = LLVMGetInsertBlock(ctx->builder);

    // Build nested function
    ProcDeclAttributes * attrs = (ProcDeclAttributes *)node->metadata;
    char const * func_name = (attrs && attrs->link_name) ? attrs->link_name : name_node->text;
    LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, proc_type->proc_metadata.func_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    // Register in enclosing scope BEFORE body generation
    // so recursive calls can find this function
    TypedValue tv = create_typed_value(func, proc_type, false);
    generator_add_symbol(ctx->gen_ctx, name_node->text, tv);

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

    // Restore outer function context
    func_pop(ctx);
    if (outer_block != NULL)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, outer_block);
    }

    return func;
}

// Postfix expression / call codegen moved to ir_gen_postfix.c
// --- Heap allocation helpers ---

// --- Heap allocation helpers moved to ir_intrinsic.c ---

// --- Type info globals ---

static int
type_info_kind_from_td_kind(td_kind_t kind)
{
    switch (kind)
    {
        case TD_KIND_BASIC: return 1;
        case TD_KIND_POINTER: return 2;
        case TD_KIND_ARRAY: return 3;
        case TD_KIND_SLICE: return 4;
        case TD_KIND_DYNAMIC_ARRAY: return 5;
        case TD_KIND_STRUCT: return 6;
        case TD_KIND_UNION: return 7;
        case TD_KIND_ENUM: return 8;
        case TD_KIND_BIT_FIELD: return 9;
        case TD_KIND_BIT_SET: return 10;
        case TD_KIND_MAP: return 11;
        case TD_KIND_PROC: return 12;
        case TD_KIND_DISTINCT: return 13;
        case TD_KIND_SOA: return 14;
        case TD_KIND_RANGE: return 15;
        case TD_KIND_MAYBE: return 16;
        case TD_KIND_MULTI_POINTER: return 17;
        case TD_KIND_VECTOR: return 18;
        default: return 0;
    }
}

static LLVMValueRef
ir_gen_get_or_create_type_info_global(IrGenContext * ctx, TypeDescriptor const * td)
{
    if (td == NULL || td->llvm_type == NULL)
        return LLVMConstPointerNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));

    // Check if we already created a global for this type
    for (int i = 0; i < ctx->type_info_global_count; i++)
    {
        if (ctx->type_info_globals[i].type_id == td->type_id)
            return ctx->type_info_globals[i].global;
    }

    if (ctx->type_info_global_count >= MAX_TYPE_INFO_GLOBALS)
        return LLVMConstPointerNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));

    // Compute size and alignment
    uint64_t size = LLVMABISizeOfType(ctx->data_layout, td->llvm_type);
    uint64_t align = LLVMABIAlignmentOfType(ctx->data_layout, td->llvm_type);
    uint64_t type_id = (uint64_t)td->type_id;
    uint64_t kind = (uint64_t)type_info_kind_from_td_kind(td->kind);

    TypeDescriptor const * ti_td = type_descriptor_get_type_info_type(ctx->type_registry);
    if (ti_td == NULL)
        return LLVMConstPointerNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
    LLVMTypeRef ti_type = ti_td->llvm_type;

    LLVMValueRef fields[4];
    fields[0] = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), size, false);
    fields[1] = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), align, false);
    fields[2] = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), type_id, false);
    fields[3] = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), kind, false);

    LLVMValueRef init_val = LLVMConstNamedStruct(ti_type, fields, 4);

    // Create a unique name for the global
    char global_name[128];
    snprintf(global_name, sizeof(global_name), "_type_info_%" PRIx64, td->type_id);

    LLVMValueRef global = LLVMAddGlobal(ctx->module, ti_type, global_name);
    LLVMSetInitializer(global, init_val);
    LLVMSetGlobalConstant(global, true);
    LLVMSetLinkage(global, LLVMInternalLinkage);
    LLVMSetUnnamedAddr(global, true);

    int idx = ctx->type_info_global_count++;
    ctx->type_info_globals[idx].type_id = td->type_id;
    ctx->type_info_globals[idx].global = global;

    return global;
}

// Phase 3.4 helpers — extracted inline cases from ir_gen_node

static LLVMValueRef
ir_gen_cast_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "cast expression has no resolved target type");
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
    return LLVMBuildBitCast(ctx->builder, src_val, dest_llvm_type, "cast");
}

static LLVMValueRef
ir_gen_len_cap_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "len/cap operand has no type");
        return NULL;
    }

    if (operand_type->kind == TD_KIND_ARRAY)
    {
        return LLVMConstInt(
            LLVMInt64TypeInContext(ctx->context), (unsigned long long)operand_type->as.array.count, false
        );
    }

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
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "len/cap: failed to extract map data pointer");
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
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "len/cap: failed to extract field from slice/dynamic array");
        return NULL;
    }
    return len_val;
}

static LLVMValueRef
ir_gen_offset_of_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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
    return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
}

static LLVMValueRef
ir_gen_raw_data_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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

static LLVMValueRef
ir_gen_make_expr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2)
        return NULL;
    odin_grammar_node_t * len_node = node->list.children[1];

    TypeDescriptor const * result_type = node->resolved_type;
    if (result_type == NULL
        || (result_type->kind != TD_KIND_SLICE && result_type->kind != TD_KIND_DYNAMIC_ARRAY
            && result_type->kind != TD_KIND_MAP))
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "make: target type is not a slice, dynamic array, or map");
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
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "make(map): key or value type is NULL");
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

        LLVMValueRef cidx = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 8, false);
        LLVMValueRef cptr = LLVMBuildPointerCast(
            ctx->builder,
            LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &cidx, 1, ""),
            LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
            ""
        );
        LLVMBuildStore(ctx->builder, len_val, cptr);

        LLVMValueRef ksid = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 16, false);
        LLVMValueRef ksptr = LLVMBuildPointerCast(
            ctx->builder,
            LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &ksid, 1, ""),
            LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
            ""
        );
        LLVMBuildStore(ctx->builder, key_size, ksptr);

        LLVMValueRef vsid = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 24, false);
        LLVMValueRef vsptr = LLVMBuildPointerCast(
            ctx->builder,
            LLVMBuildInBoundsGEP2(ctx->builder, i8t, map_data, &vsid, 1, ""),
            LLVMPointerType(LLVMInt64TypeInContext(ctx->context), 0),
            ""
        );
        LLVMBuildStore(ctx->builder, val_size, vsptr);

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
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "make(slice): element type is NULL");
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

        LLVMValueRef didx[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false)};
        LLVMBuildStore(
            ctx->builder,
            data_ptr,
            LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, didx, 2, "make.data.gep")
        );

        LLVMValueRef lidx[]
            = {LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
               LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false)};
        LLVMBuildStore(
            ctx->builder,
            len_val,
            LLVMBuildInBoundsGEP2(ctx->builder, result_type->llvm_type, make_ptr, lidx, 2, "make.len.gep")
        );

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

static LLVMValueRef
ir_gen_delete_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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

static LLVMValueRef
ir_gen_incl_excl_expr(IrGenContext * ctx, odin_grammar_node_t * node)
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
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "incl/excl: operand is not a pointer");
        return NULL;
    }
    TypeDescriptor const * bs_type = ptr_type->pointee;
    if (bs_type == NULL || bs_type->kind != TD_KIND_BIT_SET)
    {
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "incl/excl: pointee is not a bit_set");
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

static LLVMValueRef
ir_gen_compress_values_expr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2 || node->resolved_type == NULL)
        return NULL;
    TypeDescriptor const * target_type = node->resolved_type;
    LLVMTypeRef llvm_type = target_type->llvm_type;
    LLVMValueRef result = LLVMGetUndef(llvm_type);
    for (size_t i = 1; i < node->list.count; i++)
    {
        LLVMValueRef val = ir_gen_node(ctx, node->list.children[i]);
        if (val == NULL)
            return NULL;
        unsigned field_idx = (unsigned)(i - 1);
        if (target_type->kind == TD_KIND_STRUCT)
        {
            struct_field_t const * field = type_descriptor_get_struct_field(target_type, (int)field_idx);
            if (field && field->type_desc && field->type_desc->llvm_type
                && LLVMTypeOf(val) != field->type_desc->llvm_type)
            {
                val = coerce_value_to_type(ctx, val, field->type_desc->llvm_type,
                    false, "compress.field");
            }
        }
        else if (target_type->kind == TD_KIND_ARRAY && target_type->element_type
                 && target_type->element_type->llvm_type
                 && LLVMTypeOf(val) != target_type->element_type->llvm_type)
        {
            val = coerce_value_to_type(ctx, val, target_type->element_type->llvm_type,
                false, "compress.elem");
        }
        result = LLVMBuildInsertValue(ctx->builder, result, val, field_idx, "compress.field");
    }
    return result;
}

static LLVMValueRef
ir_gen_soa_zip_expr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1 || node->resolved_type == NULL)
        return NULL;
    odin_grammar_node_t * arg_list = node->list.children[0];
    odin_grammar_node_t * arg_expr = (arg_list && arg_list->list.count >= 1) ? arg_list->list.children[0] : NULL;
    if (arg_expr == NULL)
        return NULL;

    odin_grammar_node_t * ir_args[128];
    int ir_arg_count = 0;
    ir_gen_collect_comma_chain_args(arg_expr, ir_args, 128, &ir_arg_count);
    if (ir_arg_count < 1)
        return NULL;

    TypeDescriptor const * result_type = node->resolved_type;
    LLVMTypeRef llvm_type = result_type->llvm_type;

    LLVMValueRef * data_ptrs = malloc((size_t)ir_arg_count * sizeof(LLVMValueRef));
    LLVMValueRef * len_vals = malloc((size_t)ir_arg_count * sizeof(LLVMValueRef));
    if (data_ptrs == NULL || len_vals == NULL)
    {
        free(data_ptrs);
        free(len_vals);
        return NULL;
    }

    LLVMValueRef min_len = NULL;
    for (int i = 0; i < ir_arg_count; i++)
    {
        LLVMValueRef slice_val = ir_gen_node(ctx, ir_args[i]);
        if (slice_val == NULL)
        {
            free(data_ptrs);
            free(len_vals);
            return NULL;
        }
        TypeDescriptor const * arg_type = ir_args[i]->resolved_type;
        if (LLVMGetTypeKind(LLVMTypeOf(slice_val)) == LLVMPointerTypeKind
            && arg_type != NULL && arg_type->llvm_type != NULL)
        {
            slice_val = LLVMBuildLoad2(ctx->builder, arg_type->llvm_type, slice_val, "sz.load");
        }
        data_ptrs[i] = LLVMBuildExtractValue(ctx->builder, slice_val, 0, "sz.data");
        len_vals[i] = LLVMBuildExtractValue(ctx->builder, slice_val, 1, "sz.len");

        if (i == 0)
            min_len = len_vals[i];
        else
        {
            LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntULT, len_vals[i], min_len, "sz.cmp");
            min_len = LLVMBuildSelect(ctx->builder, cmp, len_vals[i], min_len, "sz.min");
        }
    }

    LLVMValueRef result = LLVMGetUndef(llvm_type);
    for (int i = 0; i < ir_arg_count; i++)
    {
        TypeDescriptor const * arg_type = ir_args[i]->resolved_type;
        LLVMValueRef new_slice = LLVMGetUndef(arg_type->llvm_type);
        new_slice = LLVMBuildInsertValue(ctx->builder, new_slice, data_ptrs[i], 0, "sz.slice.data");
        new_slice = LLVMBuildInsertValue(ctx->builder, new_slice, min_len, 1, "sz.slice.len");
        result = LLVMBuildInsertValue(ctx->builder, result, new_slice, (unsigned)i, "sz.field");
    }

    free(data_ptrs);
    free(len_vals);
    return result;
}

static LLVMValueRef
ir_gen_soa_unzip_expr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1 || node->resolved_type == NULL)
        return NULL;
    LLVMValueRef soa_val = ir_gen_node(ctx, node->list.children[0]);
    if (soa_val == NULL)
        return NULL;
    TypeDescriptor const * arg_type = node->list.children[0]->resolved_type;
    if (LLVMGetTypeKind(LLVMTypeOf(soa_val)) == LLVMPointerTypeKind
        && arg_type != NULL && arg_type->llvm_type != NULL)
    {
        soa_val = LLVMBuildLoad2(ctx->builder, arg_type->llvm_type, soa_val, "suz.load");
    }
    TypeDescriptor const * result_type = node->resolved_type;
    int field_count = arg_type->struct_metadata.members.count;
    LLVMValueRef result = LLVMGetUndef(result_type->llvm_type);
    for (int i = 0; i < field_count; i++)
    {
        LLVMValueRef field_val = LLVMBuildExtractValue(ctx->builder, soa_val, (unsigned)i, "suz.field");
        result = LLVMBuildInsertValue(ctx->builder, result, field_val, (unsigned)i, "suz.tuple");
    }
    return result;
}

static LLVMValueRef
ir_gen_directive(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text != NULL && strstr(node->text, "#no_bounds_check") != NULL)
    {
        ctx->bounds_checking_enabled = false;
        return NULL;
    }

    if (node->text != NULL && strstr(node->text, "#caller_location") != NULL)
    {
        TypeDescriptor const * sl_type = node->resolved_type;
        if (sl_type == NULL || sl_type->kind != TD_KIND_STRUCT)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "#caller_location: unresolved type");
            return NULL;
        }

        char const * file = node->file_path ? node->file_path : "<unknown>";
        size_t line = node->source_data.view.line_number;
        size_t col = node->source_data.view.column_number;

        LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
        size_t file_len = strlen(file);
        size_t arr_len = file_len + 1;
        LLVMValueRef * elements = malloc(arr_len * sizeof(LLVMValueRef));
        for (size_t i = 0; i < file_len; i++)
            elements[i] = LLVMConstInt(i8_type, (unsigned char)file[i], false);
        elements[file_len] = LLVMConstInt(i8_type, 0, false);
        LLVMTypeRef arr_type = LLVMArrayType(i8_type, arr_len);
        LLVMValueRef arr_const = LLVMConstArray(i8_type, elements, arr_len);
        free(elements);

        LLVMValueRef global = LLVMAddGlobal(ctx->module, arr_type, ".loc_file");
        LLVMSetInitializer(global, arr_const);
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
        LLVMSetGlobalConstant(global, true);

        LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
        LLVMValueRef indices[] = {zero, zero};
        LLVMValueRef ptr = LLVMConstInBoundsGEP2(arr_type, global, indices, 2);

        TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
        LLVMTypeRef str_type = str_desc ? str_desc->llvm_type : LLVMStructType(NULL, 0, false);
        LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)file_len, false);
        LLVMValueRef file_with_len[] = {ptr, len_val};
        LLVMValueRef file_val = LLVMConstNamedStruct(str_type, file_with_len, 2);

        LLVMValueRef line_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)line, false);
        LLVMValueRef col_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (unsigned long long)col, false);

        LLVMValueRef struct_vals[] = {file_val, line_val, col_val};
        return LLVMConstNamedStruct(sl_type->llvm_type, struct_vals, 3);
    }
    return NULL;
}

// --- Main node dispatcher ---

LLVMValueRef
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
        return ir_gen_string_literal(ctx, node, true);
    case AST_NODE_RAW_STRING_LITERAL:
        return ir_gen_string_literal(ctx, node, false);

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
        return ir_gen_cast_expr(ctx, node);

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
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "transmute expression has no resolved target type");
            return NULL;
        }

        LLVMTypeRef dest_llvm_type = dest_type->llvm_type;
        return LLVMBuildBitCast(ctx->builder, src_val, dest_llvm_type, "transmute");
    }

    case AST_NODE_LEN_EXPR:
    case AST_NODE_CAP_EXPR:
        return ir_gen_len_cap_expr(ctx, node);

    case AST_NODE_TYPE_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand_node = node->list.children[0];
        TypeDescriptor const * operand_type = operand_node->resolved_type;
        if (operand_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        // Runtime path: for 'any' type, extract type_id from struct field 1
        if (operand_type->kind == TD_KIND_BASIC && operand_type->as.basic.name
            && strcmp(operand_type->as.basic.name, "any") == 0)
        {
            LLVMValueRef operand = ir_gen_node(ctx, operand_node);
            if (operand == NULL)
                return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
            LLVMTypeRef any_type = operand_type->llvm_type;
            LLVMValueRef tmp = LLVMBuildAlloca(ctx->builder, any_type, "typeof.tmp");
            LLVMBuildStore(ctx->builder, operand, tmp);
            LLVMValueRef idx0 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
            LLVMValueRef idx1 = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 1, false);
            LLVMValueRef gep[2] = {idx0, idx1};
            LLVMValueRef id_field = LLVMBuildInBoundsGEP2(ctx->builder, any_type, tmp, gep, 2, "typeof.typeid");
            return LLVMBuildLoad2(ctx->builder, LLVMInt64TypeInContext(ctx->context), id_field, "typeof.tid");
        }
        // Compile-time path: emit the type_id hash directly
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)operand_type->type_id, false);
    }

    case AST_NODE_TYPEID_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand_node = node->list.children[0];
        TypeDescriptor const * operand_type = operand_node->resolved_type;
        if (operand_type == NULL)
            return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 0, false);
        return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), (uint64_t)operand_type->type_id, false);
    }

    case AST_NODE_TYPE_INFO_OF_EXPR:
    {
        if (node->list.count < 1)
            return NULL;
        odin_grammar_node_t * operand_node = node->list.children[0];
        TypeDescriptor const * operand_type = operand_node->resolved_type;
        if (operand_type == NULL)
            return LLVMConstPointerNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
        // Return a pointer to the type_info global for this type
        return ir_gen_get_or_create_type_info_global(ctx, operand_type);
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
        return ir_gen_offset_of_expr(ctx, node);

    case AST_NODE_RAW_DATA_EXPR:
        return ir_gen_raw_data_expr(ctx, node);

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
        return ir_gen_make_expr(ctx, node);

    case AST_NODE_NEW_EXPR:
    {
        if (node->list.count < 1)
            return NULL;

        TypeDescriptor const * ptr_type = node->resolved_type;
        if (ptr_type == NULL || ptr_type->kind != TD_KIND_POINTER)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "new: target type is not a pointer");
            return NULL;
        }
        TypeDescriptor const * pointee_type = ptr_type->pointee;
        if (pointee_type == NULL)
        {
            ir_gen_error_collection_add(&ctx->errors, NULL, node, "new: pointer has no pointee type");
            return NULL;
        }

        LLVMValueRef size = LLVMSizeOf(pointee_type->llvm_type);
        LLVMValueRef raw_mem = ir_gen_call_malloc(ctx, size);
        if (raw_mem == NULL)
            return NULL;
        return LLVMBuildPointerCast(ctx->builder, raw_mem, ptr_type->llvm_type, "new.result");
    }

    case AST_NODE_DELETE_EXPR:
        return ir_gen_delete_expr(ctx, node);

    case AST_NODE_INCL_EXPR:
    case AST_NODE_EXCL_EXPR:
        return ir_gen_incl_excl_expr(ctx, node);

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

    case AST_NODE_EXPAND_VALUES_EXPR:
    {
        // expand_values(x) returns the aggregate value itself.
        // The actual field/array expansion happens in ir_gen_collect_call_args.
        if (node->list.count < 1)
            return NULL;
        return ir_gen_node(ctx, node->list.children[0]);
    }

    case AST_NODE_COMPRESS_VALUES_EXPR:
        return ir_gen_compress_values_expr(ctx, node);

    case AST_NODE_SOA_ZIP_EXPR:
        return ir_gen_soa_zip_expr(ctx, node);

    case AST_NODE_SOA_UNZIP_EXPR:
        return ir_gen_soa_unzip_expr(ctx, node);

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
        ir_gen_error_collection_add(&ctx->errors, NULL, node, "'context' not available here");
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
        return ir_gen_directive(ctx, node);

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

static int ir_gen_evaluate_constant_bool(IrGenContext * ctx, odin_grammar_node_t * node);

static long long
ir_gen_evaluate_constant_int(IrGenContext * ctx, odin_grammar_node_t * node, int * ok)
{
    if (node == NULL) { *ok = 0; return 0; }

    // Unwrap through expression chain to reach a node type we can evaluate
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
        case AST_NODE_ADD_EXPRESSION:
        case AST_NODE_MUL_EXPRESSION:
        case AST_NODE_BIT_AND_EXPRESSION:
        case AST_NODE_BIT_XOR_EXPRESSION:
        case AST_NODE_BIT_OR_EXPRESSION:
        case AST_NODE_SHIFT_EXPRESSION:
        case AST_NODE_LOG_AND_EXPRESSION:
        case AST_NODE_LOG_OR_EXPRESSION:
        case AST_NODE_IDENTIFIER:
            can_eval = 1;
            break;
        case AST_NODE_POSTFIX_EXPRESSION:
            if (node->list.count >= 2 && node->list.children[1] != NULL)
            {
                odin_grammar_node_t * postfix_ops = node->list.children[1];
                if (postfix_ops->list.count > 0
                    && postfix_ops->list.children[0] != NULL
                    && postfix_ops->list.children[0]->type == AST_NODE_POSTFIX_MEMBER)
                    can_eval = 1;
            }
            break;
        default:
            break;
        }
        if (can_eval)
            break;
        if ((node->type == AST_NODE_POSTFIX_EXPRESSION || node->list.count == 1) && node->list.children[0])
            node = node->list.children[0];
        else
            { *ok = 0; return 0; }
    }

    switch (node->type)
    {
    case AST_NODE_BOOL_TRUE:
        *ok = 1; return 1;
    case AST_NODE_BOOL_FALSE:
        *ok = 1; return 0;

    case AST_NODE_IDENTIFIER:
    {
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
        if (sym != NULL && sym->has_const_int_val)
        {
            *ok = 1;
            return sym->const_int_val;
        }
        *ok = 0;
        return 0;
    }

    case AST_NODE_POSTFIX_EXPRESSION:
    {
        if (node->list.count < 2 || node->list.children[0] == NULL || node->list.children[1] == NULL)
        { *ok = 0; return 0; }

        odin_grammar_node_t * inner = node->list.children[0];
        while (inner != NULL && inner->type != AST_NODE_IDENTIFIER && inner->list.count >= 1)
            inner = inner->list.children[0];
        if (inner == NULL || inner->type != AST_NODE_IDENTIFIER)
        { *ok = 0; return 0; }

        ImportedPackage * pkg = NULL;
        for (int i = 0; i < ctx->import_count; i++)
        {
            if (ctx->imports[i] && ctx->imports[i]->package_name
                && strcmp(ctx->imports[i]->package_name, inner->text) == 0)
            {
                pkg = ctx->imports[i];
                break;
            }
        }
        if (pkg == NULL || pkg->package_scope == NULL)
        { *ok = 0; return 0; }

        odin_grammar_node_t * postfix_ops = node->list.children[1];
        if (postfix_ops == NULL || postfix_ops->list.count == 0)
        { *ok = 0; return 0; }

        odin_grammar_node_t * member_op = postfix_ops->list.children[0];
        if (member_op == NULL || member_op->type != AST_NODE_POSTFIX_MEMBER)
        { *ok = 0; return 0; }

        if (member_op->list.count < 1 || member_op->list.children[0] == NULL)
        { *ok = 0; return 0; }

        char const * member_name = member_op->list.children[0]->text;
        symbol_t * sym = scope_find_symbol_entry(pkg->package_scope, member_name);
        if (sym != NULL && sym->has_const_int_val)
        {
            *ok = 1;
            return sym->const_int_val;
        }
        *ok = 0;
        return 0;
    }

    case AST_NODE_INTEGER_VALUE:
    {
        if (node->text == NULL) { *ok = 0; return 0; }
        char * end = NULL;
        long long val = parse_odin_signed(node->text, &end, 0);
        if (end == node->text) { *ok = 0; return 0; }
        *ok = 1;
        return val;
    }

    case AST_NODE_UNARY_EXPRESSION:
    {
        odin_grammar_node_t * op_node = node_find_op(node);
        if (op_node == NULL) { *ok = 0; return 0; }
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL) { *ok = 0; return 0; }

        odin_grammar_node_t * operand = NULL;
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child != NULL && child != op_node) { operand = child; break; }
        }
        if (operand == NULL) { *ok = 0; return 0; }

        int inner_ok = 0;
        long long inner_val = ir_gen_evaluate_constant_int(ctx, operand, &inner_ok);
        if (!inner_ok) { *ok = 0; return 0; }

        switch (md->kind)
        {
        case OP_UNARY_NEG: *ok = 1; return -inner_val;
        case OP_UNARY_POS: *ok = 1; return inner_val;
        case OP_UNARY_XOR: *ok = 1; return ~inner_val;
        case OP_UNARY_NOT: *ok = 1; return inner_val ? 0 : 1;
        default: *ok = 0; return 0;
        }
    }

    case AST_NODE_ADD_EXPRESSION:
    case AST_NODE_MUL_EXPRESSION:
    case AST_NODE_BIT_AND_EXPRESSION:
    case AST_NODE_BIT_XOR_EXPRESSION:
    case AST_NODE_BIT_OR_EXPRESSION:
    case AST_NODE_SHIFT_EXPRESSION:
    case AST_NODE_COMP_EXPRESSION:
    case AST_NODE_LOG_AND_EXPRESSION:
    case AST_NODE_LOG_OR_EXPRESSION:
    {
        odin_grammar_node_t * op_node = node_find_op(node);
        if (op_node == NULL) { *ok = 0; return 0; }
        AstOpMetadata * md = (AstOpMetadata *)op_node->metadata;
        if (md == NULL) { *ok = 0; return 0; }
        if (node->list.count < 3) { *ok = 0; return 0; }

        int lhs_ok = 0, rhs_ok = 0;
        long long lhs_val = ir_gen_evaluate_constant_int(ctx, node->list.children[0], &lhs_ok);
        long long rhs_val = ir_gen_evaluate_constant_int(ctx, node->list.children[node->list.count - 1], &rhs_ok);
        if (!lhs_ok || !rhs_ok) { *ok = 0; return 0; }

        switch (md->kind)
        {
        case OP_ADD: *ok = 1; return lhs_val + rhs_val;
        case OP_SUB: *ok = 1; return lhs_val - rhs_val;
        case OP_MUL: *ok = 1; return lhs_val * rhs_val;
        case OP_DIV: if (rhs_val == 0) { *ok = 0; return 0; } *ok = 1; return lhs_val / rhs_val;
        case OP_MOD: if (rhs_val == 0) { *ok = 0; return 0; } *ok = 1; return lhs_val % rhs_val;
        case OP_SHL: *ok = 1; return lhs_val << rhs_val;
        case OP_SHR: *ok = 1; return lhs_val >> rhs_val;
        case OP_BIT_AND: *ok = 1; return lhs_val & rhs_val;
        case OP_BIT_OR:  *ok = 1; return lhs_val | rhs_val;
        case OP_BIT_XOR: *ok = 1; return lhs_val ^ rhs_val;
        case OP_EQ: *ok = 1; return (lhs_val == rhs_val) ? 1 : 0;
        case OP_NE: *ok = 1; return (lhs_val != rhs_val) ? 1 : 0;
        case OP_LT: *ok = 1; return (lhs_val < rhs_val) ? 1 : 0;
        case OP_GT: *ok = 1; return (lhs_val > rhs_val) ? 1 : 0;
        case OP_LE: *ok = 1; return (lhs_val <= rhs_val) ? 1 : 0;
        case OP_GE: *ok = 1; return (lhs_val >= rhs_val) ? 1 : 0;
        default: *ok = 0; return 0;
        }
    }

    default:
        *ok = 0;
        return 0;
    }
}

static int
ir_gen_evaluate_constant_bool(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL)
        return -1;
    int ok = 0;
    long long val = ir_gen_evaluate_constant_int(ctx, node, &ok);
    if (!ok)
        return -1;
    return (val != 0) ? 1 : 0;
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
            int cond = ir_gen_evaluate_constant_bool(ctx, wc);
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
    if (sym == NULL || sym->name == NULL || target_scope == NULL || sym->is_private)
        return;
    scope_add_symbol(target_scope, sym->name, sym->value);
    if (sym->has_const_int_val)
    {
        symbol_t * copy = scope_find_symbol_entry(target_scope, sym->name);
        if (copy)
        {
            copy->const_int_val = sym->const_int_val;
            copy->has_const_int_val = true;
        }
    }
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

    // Create argc/argv globals for os.args init (needed before main AST is processed)
    LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef argv_llvm_type = LLVMPointerType(i8ptr, 0);

    ctx->odin_argc_global = LLVMAddGlobal(ctx->module, i64t, "__odin_argc");
    LLVMSetInitializer(ctx->odin_argc_global, LLVMConstInt(i64t, 0, false));

    ctx->odin_argv_global = LLVMAddGlobal(ctx->module, argv_llvm_type, "__odin_argv");
    LLVMSetInitializer(ctx->odin_argv_global, LLVMConstNull(argv_llvm_type));

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

        LLVMTypeRef i64t = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(ctx->context);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
        LLVMTypeRef argv_llvm_type = LLVMPointerType(i8ptr, 0);

        // C main(int argc, char** argv) — matches what the C runtime expects
        LLVMTypeRef main_param_types[] = {i32t, argv_llvm_type};
        LLVMTypeRef main_type = LLVMFunctionType(i32t, main_param_types, 2, false);
        LLVMValueRef c_main = LLVMAddFunction(ctx->module, "main", main_type);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, c_main, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        // Store argc/argv to globals for Odin init code
        LLVMValueRef argc_param = LLVMGetParam(c_main, 0);
        LLVMValueRef argv_param = LLVMGetParam(c_main, 1);
        LLVMValueRef argc_i64 = LLVMBuildZExt(ctx->builder, argc_param, i64t, "argc.ext");
        LLVMBuildStore(ctx->builder, argc_i64, ctx->odin_argc_global);
        LLVMBuildStore(ctx->builder, argv_param, ctx->odin_argv_global);

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
        LLVMBuildCall2(ctx->builder, odin_main_func_type, odin_main, odin_main_args, 1, "");

        // Odin main is always void; exit code is set via os.exit()
        LLVMBuildRet(ctx->builder, LLVMConstInt(i32t, 0, false));
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
