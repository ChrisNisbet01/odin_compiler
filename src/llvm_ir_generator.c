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
    (void)ctx;
    (void)node;
    // String literals not yet implemented; return null pointer placeholder
    return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0));
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

// Handle assignment expressions: AssignExpression = OrReturnExpr (AssignOp OrReturnExpr)?
// When there's no operator (no assignment), it recurses as a normal expression.
// When there IS an operator (=, +=, etc.), it stores the RHS into the LHS alloca.
static LLVMValueRef
ir_gen_assign_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // No operator — just a plain OrReturnExpr wrapper, recurse normally
    if (node->list.count < 3)
    {
        if (node->list.count > 0)
            return ir_gen_node(ctx, node->list.children[0]);
        return NULL;
    }

    // Only handle simple = for now
    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node)
    {
        AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
        if (op_md && op_md->kind != OP_ASSIGN
            && op_md->kind != OP_ADD_ASSIGN
            && op_md->kind != OP_SUB_ASSIGN)
        {
            return NULL;
        }
    }

    // Evaluate RHS (children[2])
    LLVMValueRef rhs_val = ir_gen_node(ctx, node->list.children[2]);
    if (rhs_val == NULL) return NULL;

    // Look up LHS symbol directly and store
    odin_grammar_node_t * lhs_id = expression_unwrap_to_identifier(node->list.children[0]);
    if (lhs_id == NULL) return rhs_val;

    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), lhs_id->text);
    if (sym && sym->value.is_lvalue && sym->value.value != NULL)
    {
        return LLVMBuildStore(ctx->builder, rhs_val, sym->value.value);
    }

    return rhs_val;
}

static LLVMValueRef
ir_gen_assign_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 2) return NULL;

    odin_grammar_node_t * op_node = node_find_op(node);
    if (op_node)
    {
        AstOpMetadata * op_md = (AstOpMetadata *)op_node->metadata;
        if (op_md && op_md->kind != OP_ASSIGN
            && op_md->kind != OP_ADD_ASSIGN
            && op_md->kind != OP_SUB_ASSIGN)
        {
            return NULL;
        }
    }

    LLVMValueRef rhs_val = NULL;
    if (node->list.count >= 3)
    {
        rhs_val = ir_gen_node(ctx, node->list.children[2]);
    }
    else if (node->list.count == 2)
    {
        rhs_val = ir_gen_node(ctx, node->list.children[1]);
    }
    if (rhs_val == NULL) return NULL;

    odin_grammar_node_t * lhs_id = expression_unwrap_to_identifier(node->list.children[0]);
    if (lhs_id == NULL) return rhs_val;

    symbol_t * sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), lhs_id->text);
    if (sym && sym->value.is_lvalue && sym->value.value != NULL)
    {
        return LLVMBuildStore(ctx->builder, rhs_val, sym->value.value);
    }

    return rhs_val;
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
        ir_gen_compound_statement(ctx, body_node);
    }
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef body_term = LLVMGetLastInstruction(body_end);
    if (body_term == NULL || !LLVMIsATerminatorInst(body_term))
    {
        LLVMBuildBr(ctx->builder, cond_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

// --- Procedure literal codegen ---

static LLVMValueRef
ir_gen_procedure_literal(IrGenContext * ctx, odin_grammar_node_t * node)
{
    odin_grammar_node_t * sig_node = NULL;
    odin_grammar_node_t * body_node = NULL;

    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child->type == AST_NODE_PROCEDURE_SIGNATURE) sig_node = child;
        if (child->type == AST_NODE_COMPOUND_STATEMENT) body_node = child;
    }

    if (sig_node == NULL) return NULL;

    TypeDescriptor const * return_type = NULL;
    for (size_t i = 0; i < sig_node->list.count; i++)
    {
        odin_grammar_node_t * child = sig_node->list.children[i];
        if (child->type == AST_NODE_RETURNS && child->list.count > 0)
        {
            odin_grammar_node_t * type_expr = child->list.children[0];
            return_type = type_expr->resolved_type;
        }
    }

    if (return_type == NULL)
    {
        return_type = type_descriptor_get_void_type(ctx->type_registry);
    }

    LLVMTypeRef ret_llvm = return_type->llvm_type;
    LLVMTypeRef func_type = LLVMFunctionType(ret_llvm, NULL, 0, false);

    char const * func_name = generate_anon_name(&ctx->anon_counter, "proc");
    LLVMValueRef func = LLVMAddFunction(ctx->module, func_name, func_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);

    ctx->current_function = func;
    ctx->current_return_type = return_type;

    if (body_node)
    {
        generator_push_scope(ctx->gen_ctx);
        ir_gen_compound_statement(ctx, body_node);
        generator_pop_scope(ctx->gen_ctx);
    }

    // If the last instruction wasn't a terminator, add a default ret
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef last_inst = LLVMGetLastInstruction(current_block);
    if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
    {
        if (return_type == type_descriptor_get_void_type(ctx->type_registry))
        {
            LLVMBuildRetVoid(ctx->builder);
        }
        else
        {
            LLVMBuildRet(ctx->builder, LLVMConstNull(ret_llvm));
        }
    }

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
        odin_grammar_node_t * sig_node = node_find_child(value_node, AST_NODE_PROCEDURE_SIGNATURE);

        TypeDescriptor const * return_type = NULL;
        for (size_t i = 0; i < (sig_node ? sig_node->list.count : 0); i++)
        {
            odin_grammar_node_t * child = sig_node->list.children[i];
            if (child->type == AST_NODE_RETURNS && child->list.count > 0)
            {
                return_type = child->list.children[0]->resolved_type;
            }
        }
        if (return_type == NULL)
        {
            return_type = type_descriptor_get_void_type(ctx->type_registry);
        }

        LLVMTypeRef ret_llvm = return_type->llvm_type;
        LLVMTypeRef func_type = LLVMFunctionType(ret_llvm, NULL, 0, false);

        LLVMValueRef func = LLVMAddFunction(ctx->module, name_node->text, func_type);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, func, "entry");
        LLVMPositionBuilderAtEnd(ctx->builder, entry);

        ctx->current_function = func;
        ctx->current_return_type = return_type;

        odin_grammar_node_t * body_node = node_find_child(value_node, AST_NODE_COMPOUND_STATEMENT);
        if (body_node)
        {
            generator_push_scope(ctx->gen_ctx);
            ir_gen_compound_statement(ctx, body_node);
            generator_pop_scope(ctx->gen_ctx);
        }

        LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef last_inst = LLVMGetLastInstruction(cur_block);
        if (last_inst == NULL || !LLVMIsATerminatorInst(last_inst))
        {
            if (return_type == type_descriptor_get_void_type(ctx->type_registry))
            {
                LLVMBuildRetVoid(ctx->builder);
            }
            else
            {
                LLVMBuildRet(ctx->builder, LLVMConstNull(ret_llvm));
            }
        }

        // Update the symbol with the LLVM function value
        TypedValue tv = create_typed_value(func, return_type, false);
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

        // Wrapper expression nodes — delegate to first child
        case AST_NODE_EXPRESSION:
        case AST_NODE_OR_RETURN:
        case AST_NODE_OR_ELSE:
        case AST_NODE_TERNARY_EXPRESSION:
        case AST_NODE_PRIMARY_EXPRESSION:
        case AST_NODE_POSTFIX_EXPRESSION:
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
            return ir_gen_compound_statement(ctx, node);

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

        case AST_NODE_CONSTANT_DECL:
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
