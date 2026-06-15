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

// --- Context lifecycle ---

IrGenContext *
ir_gen_context_create(char const * module_name, TypeDescriptors * type_registry, GeneratorContext * gen_ctx)
{
    IrGenContext * ctx = calloc(1, sizeof(IrGenContext));
    if (ctx == NULL) return NULL;

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
                target, triple, "generic", "",
                LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
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
    if (ctx == NULL) return;
    // Note: builder and context are owned by main.c (via gen_ctx), not disposed here
    if (ctx->module) LLVMDisposeModule(ctx->module);
    free(ctx);
}

// --- Expression codegen ---

static LLVMValueRef
ir_gen_integer_value(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL) return NULL;

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

    if (sym == NULL) return NULL;

    if (sym->value.is_lvalue && sym->value.value != NULL)
    {
        // Don't load composite types — the pointer is needed for GEP/subscript/member access
        if (sym->value.type_info
            && (sym->value.type_info->kind == TD_KIND_ARRAY
                || sym->value.type_info->kind == TD_KIND_SLICE
                || sym->value.type_info->kind == TD_KIND_STRUCT))
        {
            return sym->value.value;
        }

        // Load from alloca to get the value
        // Use the type from type_info (not LLVMGetElementType, which breaks with opaque pointers)
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
    if (node == NULL) return NULL;

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
    if (node->text == NULL) return NULL;
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
    if (node->text == NULL) return LLVMConstNull(LLVMStructType(NULL, 0, false));

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
    if (elements == NULL) return NULL;

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
    LLVMValueRef indices[] = { zero, zero };
    LLVMTypeRef i8_ptr_type = LLVMPointerType(i8_type, 0);
    LLVMValueRef ptr = LLVMConstInBoundsGEP2(arr_type, global, indices, 2);

    // Build {i8*, i64} string struct as a constant
    TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
    LLVMTypeRef str_type = str_desc ? str_desc->llvm_type : LLVMStructType(NULL, 0, false);

    LLVMValueRef len_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), content_len, false);
    LLVMValueRef struct_vals[] = { ptr, len_val };
    LLVMValueRef str_val = LLVMConstNamedStruct(str_type, struct_vals, 2);

    return str_val;
}

static LLVMValueRef
ir_gen_rune_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->text == NULL) return NULL;
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
ir_gen_binary_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node == NULL)
    {
        // Single operand — chainl1 wrapper, recurse into first child
        if (node->list.count > 0) return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    if (node->list.count < 3) return NULL;

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL) return NULL;

    LLVMValueRef lhs = ir_gen_node(ctx, node->list.children[0]);
    LLVMValueRef rhs = ir_gen_node(ctx, node->list.children[node->list.count - 1]);
    if (lhs == NULL || rhs == NULL) return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

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
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealOEQ, lhs, rhs, "cmptmp")
                : LLVMBuildICmp(ctx->builder, LLVMIntEQ, lhs, rhs, "cmptmp");
            return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
        }
        case OP_NE:
        {
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealONE, lhs, rhs, "cmptmp")
                : LLVMBuildICmp(ctx->builder, LLVMIntNE, lhs, rhs, "cmptmp");
            return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
        }
        case OP_LT:
        {
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealOLT, lhs, rhs, "cmptmp")
                : LLVMBuildICmp(ctx->builder, LLVMIntSLT, lhs, rhs, "cmptmp");
            return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
        }
        case OP_GT:
        {
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealOGT, lhs, rhs, "cmptmp")
                : LLVMBuildICmp(ctx->builder, LLVMIntSGT, lhs, rhs, "cmptmp");
            return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
        }
        case OP_LE:
        {
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealOLE, lhs, rhs, "cmptmp")
                : LLVMBuildICmp(ctx->builder, LLVMIntSLE, lhs, rhs, "cmptmp");
            return LLVMBuildIntCast2(ctx->builder, cmp, lhs_type, false, "cmpext");
        }
        case OP_GE:
        {
            LLVMValueRef cmp = is_float
                ? LLVMBuildFCmp(ctx->builder, LLVMRealOGE, lhs, rhs, "cmptmp")
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
        default:
            return NULL;
    }
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
        if (node->list.count > 0) return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
    if (op_md == NULL) return NULL;

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
    if (operand_node == NULL) return NULL;

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
                    symbol_t * sym = scope_find_symbol_entry(
                        generator_current_scope(ctx->gen_ctx), ident->text);
                    if (sym && sym->value.is_lvalue) ptr = sym->value.value;
                }
            }
            return ptr;
        }

        case OP_UNARY_DEREF:
        {
            LLVMValueRef operand = ir_gen_node(ctx, operand_node);
            if (operand == NULL) return NULL;
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
            if (operand == NULL) return NULL;

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
                    return LLVMBuildNot(ctx->builder, operand, "nottmp");
                case OP_UNARY_XOR:
                    return LLVMBuildNot(ctx->builder, operand, "xortmp");
                default:
                    return NULL;
            }
        }
    }
}

// --- Variable codegen ---

static LLVMValueRef
ir_gen_variable_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1) return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER) return NULL;

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
                LLVMBuildStore(ctx->builder, init_val, alloca);
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

    return alloca;
}

// --- Assignment codegen ---

// Recursively unwrap expression wrapper nodes to find the identifier child.
// Wrapper nodes simply delegate to their first child.
static bool
is_expression_wrapper_type(odin_grammar_node_type_t type)
{
    switch (type)
    {
        case AST_NODE_EXPRESSION:
        case AST_NODE_ASSIGN_EXPRESSION:
        case AST_NODE_OR_RETURN:
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

// Evaluate a node as an lvalue (return a pointer to the storage location).
static LLVMValueRef
ir_gen_lvalue(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

    switch (node->type)
    {
        case AST_NODE_IDENTIFIER:
        {
            symbol_t * sym = scope_find_symbol_entry(
                generator_current_scope(ctx->gen_ctx), node->text);
            if (sym && sym->value.is_lvalue) return sym->value.value;
            return NULL;
        }

        case AST_NODE_POSTFIX_EXPRESSION:
        {
            if (node->list.count < 2) return ir_gen_lvalue(ctx, node->list.children[0]);
            odin_grammar_node_t * base = node->list.children[0];
            odin_grammar_node_t * postfix_ops = node->list.children[1];
            if (postfix_ops == NULL) return ir_gen_lvalue(ctx, base);

            LLVMValueRef ptr = ir_gen_lvalue(ctx, base);
            if (ptr == NULL) return NULL;

            TypeDescriptor const * cur_type = base ? base->resolved_type : NULL;

            for (size_t i = 0; i < postfix_ops->list.count; i++)
            {
                odin_grammar_node_t * op = postfix_ops->list.children[i];
                if (op == NULL) continue;

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
                        if (index_expr == NULL) return NULL;

                        LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
                        if (index_val == NULL) return NULL;

                        if (cur_type == NULL || cur_type->kind != TD_KIND_ARRAY) return NULL;

                        LLVMTypeRef arr_type = cur_type->llvm_type;
                        LLVMValueRef indices[] = {
                            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                            index_val
                        };
                        ptr = LLVMBuildInBoundsGEP2(
                            ctx->builder, arr_type, ptr, indices, 2, "subs");

                        if (cur_type->element_type)
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
                        if (field_name_node == NULL || field_name_node->text == NULL) return NULL;

                        if (cur_type == NULL || cur_type->kind != TD_KIND_STRUCT) return NULL;

                        field_access_path_t path;
                        if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
                            return NULL;

                        int n_indices = path.count + 1;
                        LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
                        indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                        for (int pi = 0; pi < path.count; pi++)
                        {
                            indices[pi + 1] = LLVMConstInt(
                                LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
                        }
                        ptr = LLVMBuildInBoundsGEP2(
                            ctx->builder, cur_type->llvm_type, ptr, indices, (unsigned)n_indices,
                            field_name_node->text);

                        // Update cur_type to the final field type
                        TypeDescriptor const * tmp_type = cur_type;
                        for (int pi = 0; pi < path.count; pi++)
                        {
                            struct_field_t const * f = type_descriptor_get_struct_field(tmp_type, path.indices[pi]);
                            if (f == NULL) break;
                            if (pi == path.count - 1)
                                cur_type = f->type_desc;
                            else
                                tmp_type = f->type_desc;
                        }
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
}

static OperatorKind
compound_assign_to_binary_op(OperatorKind compound_kind)
{
    switch (compound_kind)
    {
        case OP_ADD_ASSIGN: return OP_ADD;
        case OP_SUB_ASSIGN: return OP_SUB;
        case OP_MUL_ASSIGN: return OP_MUL;
        case OP_DIV_ASSIGN: return OP_DIV;
        case OP_MOD_ASSIGN: return OP_MOD;
        case OP_AND_ASSIGN: return OP_BIT_AND;
        case OP_OR_ASSIGN:  return OP_BIT_OR;
        case OP_XOR_ASSIGN: return OP_BIT_XOR;
        case OP_SHL_ASSIGN: return OP_SHL;
        case OP_SHR_ASSIGN: return OP_SHR;
        default:            return OP_INVALID;
    }
}

static LLVMValueRef
ir_gen_binary_op_by_kind(IrGenContext * ctx, LLVMValueRef lhs, LLVMValueRef rhs, OperatorKind kind)
{
    if (lhs == NULL || rhs == NULL) return NULL;

    LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
    LLVMTypeKind type_kind = LLVMGetTypeKind(lhs_type);
    bool is_float = (type_kind == LLVMFloatTypeKind || type_kind == LLVMDoubleTypeKind);

    switch (kind)
    {
        case OP_ADD:     return is_float ? LLVMBuildFAdd(ctx->builder, lhs, rhs, "addtmp") : LLVMBuildAdd(ctx->builder, lhs, rhs, "addtmp");
        case OP_SUB:     return is_float ? LLVMBuildFSub(ctx->builder, lhs, rhs, "subtmp") : LLVMBuildSub(ctx->builder, lhs, rhs, "subtmp");
        case OP_MUL:     return is_float ? LLVMBuildFMul(ctx->builder, lhs, rhs, "multmp") : LLVMBuildMul(ctx->builder, lhs, rhs, "multmp");
        case OP_DIV:     return is_float ? LLVMBuildFDiv(ctx->builder, lhs, rhs, "divtmp") : LLVMBuildSDiv(ctx->builder, lhs, rhs, "divtmp");
        case OP_MOD:     return is_float ? LLVMBuildFRem(ctx->builder, lhs, rhs, "modtmp") : LLVMBuildSRem(ctx->builder, lhs, rhs, "modtmp");
        case OP_SHL:     return LLVMBuildShl(ctx->builder, lhs, rhs, "shltmp");
        case OP_SHR:     return LLVMBuildAShr(ctx->builder, lhs, rhs, "shrtmp");
        case OP_BIT_AND: return LLVMBuildAnd(ctx->builder, lhs, rhs, "andtmp");
        case OP_BIT_OR:  return LLVMBuildOr(ctx->builder, lhs, rhs, "ortmp");
        case OP_BIT_XOR: return LLVMBuildXor(ctx->builder, lhs, rhs, "xortmp");
        default:         return NULL;
    }
}

static LLVMValueRef
ir_gen_lvalue_ptr(IrGenContext * ctx, odin_grammar_node_t * node)
{
    LLVMValueRef lhs_ptr = ir_gen_lvalue(ctx, node);
    if (lhs_ptr == NULL)
    {
        odin_grammar_node_t * lhs_id = expression_unwrap_to_identifier(node);
        if (lhs_id == NULL) return NULL;
        symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), lhs_id->text);
        if (sym && sym->value.is_lvalue) lhs_ptr = sym->value.value;
    }
    return lhs_ptr;
}

// Handle assignment expressions: AssignExpression = OrReturnExpr (AssignOp OrReturnExpr)?
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
    if (rhs_val == NULL) return NULL;

    LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, node->list.children[0]);
    if (lhs_ptr == NULL) return rhs_val;

    LLVMValueRef store_val = rhs_val;
    if (op_kind != OP_ASSIGN)
    {
        OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
        TypeDescriptor const * lhs_type_desc = NULL;
        odin_grammar_node_t * lhs_expr = node->list.children[0];
        // Walk wrappers to find resolved_type
        odin_grammar_node_t * t = lhs_expr;
        while (t && t->list.count >= 1 && t->list.children[0]) t = t->list.children[0];
        if (t) lhs_type_desc = t->resolved_type;
        if (lhs_type_desc == NULL) lhs_type_desc = type_descriptor_get_int64_type(ctx->type_registry);

        LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, lhs_type_desc->llvm_type, lhs_ptr, "loadtmp");
        store_val = ir_gen_binary_op_by_kind(ctx, lhs_val, rhs_val, bin_op);
    }

    return LLVMBuildStore(ctx->builder, store_val, lhs_ptr);
}

static LLVMValueRef
ir_gen_assign_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2) return NULL;

    odin_grammar_node_t * op_node = node_find_op(node);
    AstOpMetadata * op_md = (op_node) ? (AstOpMetadata *)op_node->metadata : NULL;
    OperatorKind op_kind = op_md ? op_md->kind : OP_ASSIGN;

    if (op_kind != OP_ASSIGN && compound_assign_to_binary_op(op_kind) == OP_INVALID)
        return NULL;

    LLVMValueRef rhs_val = ir_gen_node(ctx, node->list.children[2]);
    if (rhs_val == NULL) return NULL;

    LLVMValueRef lhs_ptr = ir_gen_lvalue_ptr(ctx, node->list.children[0]);
    if (lhs_ptr == NULL) return rhs_val;

    LLVMValueRef store_val = rhs_val;
    if (op_kind != OP_ASSIGN)
    {
        OperatorKind bin_op = compound_assign_to_binary_op(op_kind);
        TypeDescriptor const * lhs_type_desc = NULL;
        odin_grammar_node_t * lhs_expr = node->list.children[0];
        odin_grammar_node_t * t = lhs_expr;
        while (t && t->list.count >= 1 && t->list.children[0]) t = t->list.children[0];
        if (t) lhs_type_desc = t->resolved_type;
        if (lhs_type_desc == NULL) lhs_type_desc = type_descriptor_get_int64_type(ctx->type_registry);

        LLVMValueRef lhs_val = LLVMBuildLoad2(ctx->builder, lhs_type_desc->llvm_type, lhs_ptr, "loadtmp");
        store_val = ir_gen_binary_op_by_kind(ctx, lhs_val, rhs_val, bin_op);
    }

    return LLVMBuildStore(ctx->builder, store_val, lhs_ptr);
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
    if (expr == NULL)
    {
        return LLVMBuildRetVoid(ctx->builder);
    }

    LLVMValueRef val = ir_gen_node(ctx, expr);
    if (val == NULL) return NULL;

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
    if (node->list.count < 1) return NULL;

    odin_grammar_node_t * cond_node = node->list.children[0];
    odin_grammar_node_t * then_node = (node->list.count > 1) ? node->list.children[1] : NULL;
    odin_grammar_node_t * else_node = (node->list.count > 2) ? node->list.children[2] : NULL;

    if (cond_node == NULL) return NULL;

    LLVMValueRef cond_val = ir_gen_node(ctx, cond_node);
    if (cond_val == NULL) return NULL;

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
    LLVMBasicBlockRef else_bb = else_node ? LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "else") : NULL;
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
    if (node->list.count < 1) return NULL;

    odin_grammar_node_t * cond_node = NULL;
    odin_grammar_node_t * body_node = NULL;

    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL) continue;
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
    if (ctx->loop_depth > 0) ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

static LLVMValueRef
ir_gen_switch_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // SwitchStatement = KwSwitch Directive? Expression? SwitchBody
    // Children: Expression (optional), SwitchCaseClause(s), SwitchDefaultClause (optional)
    odin_grammar_node_t * switch_expr = NULL;
    odin_grammar_node_t * default_case = NULL;
    odin_grammar_node_t * case_clauses[64];
    int case_count = 0;

    // Separate children into expression, cases, and default
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL) continue;
        if (child->type == AST_NODE_SWITCH_CASE)
        {
            if (case_count < 64) case_clauses[case_count++] = child;
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

    if (switch_expr == NULL) return NULL;

    LLVMValueRef switch_val = ir_gen_node(ctx, switch_expr);
    if (switch_val == NULL) return NULL;

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, ctx->current_function, "swend");

    // Push loop context for break in switch
    if (ctx->loop_depth < MAX_LOOP_DEPTH)
    {
        ctx->loop_stack[ctx->loop_depth].continue_bb = NULL;
        ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
        ctx->loop_depth++;
    }

    // Create a block for each case + a default block
    LLVMBasicBlockRef case_bbs[64];
    LLVMBasicBlockRef next_case_bb = NULL;
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
    LLVMBasicBlockRef prev_miss_bb = NULL;
    for (int i = 0; i < case_count; i++)
    {
        odin_grammar_node_t * case_clause = case_clauses[i];
        // First child of SwitchCaseClause is KwCase (terminal, no child),
        // then Expression(s) (could be multiple comma-separated values),
        // then Statement*s.
        // The case clause body is embedded in this node (not a separate compound stmt).

        if (case_clause->list.count < 1) continue;

        odin_grammar_node_t * case_val_node = NULL;
        // Find the first expression child for the case value
        for (size_t ci = 0; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * child = case_clause->list.children[ci];
            if (child == NULL) continue;
            if (child->type != AST_NODE_COMPOUND_STATEMENT
                && child->type != AST_NODE_RETURN_STATEMENT
                && child->type != AST_NODE_BREAK_STATEMENT
                && child->type != AST_NODE_CONTINUE_STATEMENT
                && child->type != AST_NODE_EXPRESSION_STATEMENT
                && child->type != AST_NODE_ASSIGN_STATEMENT
                && child->type != AST_NODE_VARIABLE_DECL
                && child->type != AST_NODE_IF_STATEMENT
                && child->type != AST_NODE_FOR_STATEMENT
                && child->type != AST_NODE_SWITCH_STATEMENT)
            {
                case_val_node = child;
                break;
            }
        }

        if (case_val_node == NULL) continue;

        LLVMValueRef case_val = ir_gen_node(ctx, case_val_node);
        if (case_val == NULL) continue;

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
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 1; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * stmt = case_clause->list.children[ci];
            if (stmt == NULL) continue;
            if (stmt == case_val_node) continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef case_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef case_term = LLVMGetLastInstruction(case_block_end);
        if (case_term == NULL || !LLVMIsATerminatorInst(case_term))
        {
            LLVMBuildBr(ctx->builder, end_bb);
        }
        generator_pop_scope(ctx->gen_ctx);

        // Position builder at the miss block for next iteration
        if (i < case_count - 1)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, miss_bb);
            prev_miss_bb = miss_bb;
        }
    }

    // Default case — always position at default_bb
    if (default_bb)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 0; ci < default_case->list.count; ci++)
        {
            odin_grammar_node_t * stmt = default_case->list.children[ci];
            if (stmt == NULL) continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef def_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef def_term = LLVMGetLastInstruction(def_block_end);
        if (def_term == NULL || !LLVMIsATerminatorInst(def_term))
        {
            LLVMBuildBr(ctx->builder, end_bb);
        }
        generator_pop_scope(ctx->gen_ctx);
    }

    // Pop loop context
    if (ctx->loop_depth > 0) ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

// --- Procedure parameter registration ---

static void
ir_gen_register_params(IrGenContext * ctx, odin_grammar_node_t * proc_literal, LLVMValueRef func)
{
    odin_grammar_node_t * sig_node = node_find_child(proc_literal, AST_NODE_PROCEDURE_SIGNATURE);
    if (sig_node == NULL) return;

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
    if (param_list_node == NULL || param_list_node->list.count == 0) return;

    odin_grammar_node_t * params = param_list_node->list.children[0];
    if (params == NULL || params->type != AST_NODE_PARAMETERS) return;

    unsigned param_index = 0;
    for (size_t k = 0; k < params->list.count; k++)
    {
        odin_grammar_node_t * param = params->list.children[k];
        if (param == NULL || param->type != AST_NODE_PARAMETER) continue;

        odin_grammar_node_t * param_ident = NULL;
        odin_grammar_node_t * param_type_node = NULL;
        for (size_t ci = 0; ci < param->list.count; ci++)
        {
            odin_grammar_node_t * child = param->list.children[ci];
            if (child == NULL) continue;
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
                if (child == NULL) continue;
                if (child->type == AST_NODE_IDENTIFIER && child != param_ident)
                {
                    param_type_node = child;
                    break;
                }
            }
        }
        if (param_ident == NULL || param_type_node == NULL) continue;

        TypeDescriptor const * param_type = param_type_node->resolved_type;
        if (param_type == NULL) continue;

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

// --- Procedure literal codegen ---

static LLVMValueRef
ir_gen_procedure_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    odin_grammar_node_t * body_node = node_find_child(node, AST_NODE_COMPOUND_STATEMENT);

    TypeDescriptor const * proc_type = node->resolved_type;
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC) return NULL;

    LLVMValueRef func = LLVMAddFunction(ctx->module,
        generate_anon_name(&ctx->anon_counter, "proc"),
        proc_type->llvm_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    ctx->current_function = func;
    ctx->current_return_type = proc_type->proc_metadata.return_type;

    generator_push_scope(ctx->gen_ctx);
    ir_gen_register_params(ctx, node, func);

    if (body_node)
    {
        ir_gen_compound_statement(ctx, body_node);
    }

    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef last_inst = LLVMGetLastInstruction(current_block);
    if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
    {
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
    ctx->current_function = NULL;
    ctx->current_return_type = NULL;

    return func;
}

// --- Top-level declaration codegen ---

static LLVMValueRef
ir_gen_top_level_decl(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2) return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    odin_grammar_node_t * value_node = node->list.children[1];

    if (name_node->type != AST_NODE_IDENTIFIER) return NULL;

    if (value_node->type == AST_NODE_PROCEDURE_LITERAL)
    {
        TypeDescriptor const * proc_type = value_node->resolved_type;
        if (proc_type == NULL || proc_type->kind != TD_KIND_PROC) return NULL;

        LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, proc_type->llvm_type);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        ctx->current_function = func;
        ctx->current_return_type = proc_type->proc_metadata.return_type;

        odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);

        generator_push_scope(ctx->gen_ctx);
        ir_gen_register_params(ctx, value_node, func);

        if (body_node)
        {
            ir_gen_compound_statement(ctx, body_node);
        }

        LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
        if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
        {
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
    if (node->list.count < 1) return NULL;

    odin_grammar_node_t * name_node = node->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER) return NULL;

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
    if (node->list.count < 2) return NULL;
    odin_grammar_node_t * name_node = node->list.children[0];
    odin_grammar_node_t * value_node = node->list.children[1];
    if (name_node->type != AST_NODE_IDENTIFIER) return NULL;

    if (value_node->type != AST_NODE_PROCEDURE_LITERAL) return NULL;

    TypeDescriptor const * proc_type = value_node->resolved_type;
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC) return NULL;

    // Save outer function context
    LLVMValueRef outer_func = ctx->current_function;
    TypeDescriptor const * outer_ret_type = ctx->current_return_type;
    LLVMBasicBlockRef outer_block = LLVMGetInsertBlock(ctx->builder);

    // Build nested function
    LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, proc_type->llvm_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    ctx->current_function = func;
    ctx->current_return_type = proc_type->proc_metadata.return_type;

    generator_push_scope(ctx->gen_ctx);
    ir_gen_register_params(ctx, value_node, func);

    odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);
    if (body_node)
    {
        ir_gen_compound_statement(ctx, body_node);
    }

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
    if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
    {
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
    if (node == NULL || max_args <= 0) return 0;

    // Detect comma-chainl1: AST_NODE_EXPRESSION with >=2 children
    if (node->type == AST_NODE_EXPRESSION && node->list.count >= 2)
    {
        // children[0] is the left sub-chain, children[last] is the rightmost operand
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
    if (node->list.count < 1) return NULL;

    LLVMValueRef val = ir_gen_node(ctx, node->list.children[0]);

    if (node->list.count < 2) return val;
    odin_grammar_node_t * postfix_ops = node->list.children[1];
    if (postfix_ops == NULL || postfix_ops->type != AST_NODE_POSTFIX_OPS) return val;

    TypeDescriptor const * cur_type = node->list.children[0] ? node->list.children[0]->resolved_type : NULL;

    for (size_t i = 0; i < postfix_ops->list.count; i++)
    {
        odin_grammar_node_t * op = postfix_ops->list.children[i];
        if (op == NULL) continue;

        switch (op->type)
        {
            case AST_NODE_POSTFIX_CALL:
            {
                odin_grammar_node_t * ident = expression_unwrap_to_identifier(
                    node->list.children[0]);
                TypeDescriptor const * proc_type = NULL;
                if (ident)
                {
                    symbol_t * sym = scope_find_symbol_entry(
                        generator_current_scope(ctx->gen_ctx), ident->text);
                    if (sym) proc_type = sym->value.type_info;
                }
                if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
                {
                    if (cur_type && cur_type->kind == TD_KIND_PROC)
                        proc_type = cur_type;
                }
                if (proc_type == NULL || proc_type->kind != TD_KIND_PROC) return val;

                LLVMTypeRef func_type = proc_type->llvm_type;

                LLVMValueRef args[128];
                int arg_count = 0;

                if (op->list.count > 0 && op->list.children[0] != NULL)
                {
                    odin_grammar_node_t * arg_expr = op->list.children[0];
                    if (arg_expr->type == AST_NODE_ARGUMENT_LIST && arg_expr->list.count > 0)
                        arg_expr = arg_expr->list.children[0];
                    arg_count = ir_gen_collect_call_args(
                        ctx, arg_expr, args, 128);
                }

                val = LLVMBuildCall2(ctx->builder, func_type, val,
                                    args, (unsigned)arg_count, "calltmp");

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
                if (index_expr == NULL) break;

                LLVMValueRef index_val = ir_gen_node(ctx, index_expr);
                if (index_val == NULL) break;

                if (cur_type == NULL || cur_type->kind != TD_KIND_ARRAY) break;

                LLVMTypeRef arr_type = cur_type->llvm_type;
                LLVMValueRef indices[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false),
                    index_val
                };
                LLVMValueRef elem_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, arr_type, val, indices, 2, "subs");
                val = elem_ptr;

                if (cur_type->element_type)
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
                if (field_name_node == NULL || field_name_node->text == NULL) break;

                if (cur_type == NULL || cur_type->kind != TD_KIND_STRUCT) break;

                field_access_path_t path;
                if (!type_descriptor_find_struct_field_path(cur_type, field_name_node->text, &path))
                    break;

                int n_indices = path.count + 1;
                LLVMValueRef indices[MAX_FIELD_ACCESS_DEPTH + 1];
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, false);
                for (int pi = 0; pi < path.count; pi++)
                {
                    indices[pi + 1] = LLVMConstInt(
                        LLVMInt32TypeInContext(ctx->context), (unsigned)path.indices[pi], false);
                }

                LLVMValueRef field_ptr = LLVMBuildInBoundsGEP2(
                    ctx->builder, cur_type->llvm_type, val, indices, (unsigned)n_indices,
                    field_name_node->text);
                val = field_ptr;

                // Update cur_type to the final field type
                TypeDescriptor const * tmp_type = cur_type;
                for (int pi = 0; pi < path.count; pi++)
                {
                    struct_field_t const * f = type_descriptor_get_struct_field(tmp_type, path.indices[pi]);
                    if (f == NULL) break;
                    if (pi == path.count - 1)
                        cur_type = f->type_desc;
                    else
                        tmp_type = f->type_desc;
                }
                break;
            }

            default:
                break;
        }
    }

    // If the final value is a pointer and the result type is non-composite, load it
    if (val != NULL && cur_type != NULL)
    {
        LLVMTypeRef val_llvm_type = LLVMTypeOf(val);
        if (LLVMGetTypeKind(val_llvm_type) == LLVMPointerTypeKind)
        {
            if (cur_type->kind != TD_KIND_STRUCT && cur_type->kind != TD_KIND_ARRAY && cur_type->kind != TD_KIND_SLICE)
            {
                val = LLVMBuildLoad2(ctx->builder, cur_type->llvm_type, val, "loadtmp");
            }
        }
    }

    return val;
}

// --- Main node dispatcher ---

static LLVMValueRef
ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

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

        // Wrapper expression nodes — delegate to first child
        case AST_NODE_EXPRESSION:
        case AST_NODE_OR_RETURN:
        case AST_NODE_OR_ELSE:
        case AST_NODE_TERNARY_EXPRESSION:
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
            generator_push_scope(ctx->gen_ctx);
            ir_gen_compound_statement(ctx, node);
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
            return ir_gen_if_statement(ctx, node);

        case AST_NODE_FOR_STATEMENT:
            return ir_gen_for_statement(ctx, node);

        case AST_NODE_SWITCH_STATEMENT:
            return ir_gen_switch_statement(ctx, node);

        case AST_NODE_BREAK_STATEMENT:
            if (ctx->loop_depth > 0)
            {
                LLVMBuildBr(ctx->builder, ctx->loop_stack[ctx->loop_depth - 1].break_bb);
            }
            return NULL;

        case AST_NODE_CONTINUE_STATEMENT:
            if (ctx->loop_depth > 0)
            {
                LLVMBuildBr(ctx->builder, ctx->loop_stack[ctx->loop_depth - 1].continue_bb);
            }
            return NULL;

        case AST_NODE_CONSTANT_DECL:
            if (ctx->current_function != NULL)
                return ir_gen_nested_procedure_decl(ctx, node);
            return ir_gen_top_level_decl(ctx, node);

        default:
            return NULL;
    }
}

// --- Main entry point ---

bool
ir_generate(IrGenContext * ctx, odin_grammar_node_t * ast)
{
    if (ctx == NULL || ast == NULL) return false;

    for (size_t i = 0; i < ast->list.count; i++)
    {
        odin_grammar_node_t * ext_decl = ast->list.children[i];
        if (ext_decl == NULL || ext_decl->type != AST_NODE_EXTERNAL_DECLARATIONS) continue;

        for (size_t j = 0; j < ext_decl->list.count; j++)
        {
            odin_grammar_node_t * top_decl = ext_decl->list.children[j];
            if (top_decl == NULL) continue;

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
    if (ir_str == NULL) return -1;

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
        target, triple, march ? march : "generic", "",
        LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault
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
