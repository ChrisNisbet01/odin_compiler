#include "llvm_ir_generator.h"

#include "ir_utils.h"
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

    ctx->context = LLVMContextCreate();
    if (ctx->context == NULL)
    {
        free(ctx);
        return NULL;
    }

    ctx->module = LLVMModuleCreateWithNameInContext(module_name, ctx->context);
    if (ctx->module == NULL)
    {
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
    }

    ctx->builder = LLVMCreateBuilderInContext(ctx->context);
    if (ctx->builder == NULL)
    {
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        free(ctx);
        return NULL;
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
    if (ctx->builder) LLVMDisposeBuilder(ctx->builder);
    if (ctx->module) LLVMDisposeModule(ctx->module);
    if (ctx->context) LLVMContextDispose(ctx->context);
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
    symbol_t * sym = node->resolved_symbol;
    if (sym == NULL)
    {
        sym = scope_find_symbol_entry(generator_current_scope(ctx->gen_ctx), node->text);
    }

    if (sym == NULL) return NULL;

    LLVMValueRef val = sym->value.value;
    return val;
}

static LLVMValueRef
ir_gen_expression(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

    // If node has a single child with resolved type, recurse
    if (node->list.count == 1 && node->list.children[0] != NULL)
    {
        return ir_gen_node(ctx, node->list.children[0]);
    }

    return NULL;
}

// --- Statement codegen ---

static LLVMValueRef
ir_gen_return_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count == 0)
    {
        return LLVMBuildRetVoid(ctx->builder);
    }

    LLVMValueRef val = ir_gen_node(ctx, node->list.children[0]);
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
        ir_gen_compound_statement(ctx, body_node);
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
        odin_grammar_node_t * sig_node = NULL;
        for (size_t i = 0; i < value_node->list.count; i++)
        {
            if (value_node->list.children[i]->type == AST_NODE_PROCEDURE_SIGNATURE)
            {
                sig_node = value_node->list.children[i];
                break;
            }
        }

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

        odin_grammar_node_t * body_node = NULL;
        for (size_t i = 0; i < value_node->list.count; i++)
        {
            if (value_node->list.children[i]->type == AST_NODE_COMPOUND_STATEMENT)
            {
                body_node = value_node->list.children[i];
                break;
            }
        }

        if (body_node)
        {
            ir_gen_compound_statement(ctx, body_node);
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

// --- Main node dispatcher ---

static LLVMValueRef
ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node == NULL) return NULL;

    switch (node->type)
    {
        case AST_NODE_INTEGER_VALUE:
            return ir_gen_integer_value(ctx, node);

        case AST_NODE_IDENTIFIER:
            return ir_gen_identifier(ctx, node);

        case AST_NODE_EXPRESSION:
            return ir_gen_expression(ctx, node);

        case AST_NODE_RETURN_STATEMENT:
            return ir_gen_return_statement(ctx, node);

        case AST_NODE_COMPOUND_STATEMENT:
            return ir_gen_compound_statement(ctx, node);

        case AST_NODE_EXPRESSION_STATEMENT:
            if (node->list.count > 0)
                return ir_gen_node(ctx, node->list.children[0]);
            return NULL;

        case AST_NODE_TOP_LEVEL_DECLARATION:
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

            if (top_decl->type == AST_NODE_TOP_LEVEL_DECLARATION)
            {
                ir_gen_top_level_decl(ctx, top_decl);
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
