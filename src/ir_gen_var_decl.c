#include "ir_gen_var_decl.h"

#include "ast_utils.h"
#include "generator_lists.h"
#include "ir_intrinsic.h"
#include "ir_utils.h"
#include "typed_value.h"

#include <string.h>

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

LLVMValueRef
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

    // Multi-return / tuple destructuring: a, b := foo()  or  a, b := tuple_val
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
            TypeDescriptor const * field_type = NULL;
            if (var_type->kind == TD_KIND_PROC && var_type->proc_metadata.return_count > (int)i)
            {
                field_type = var_type->proc_metadata.returns[i];
            }
            else if (var_type->kind == TD_KIND_TUPLE && var_type->as.tuple.element_count > (int)i)
            {
                field_type = var_type->as.tuple.element_types[i];
            }
            LLVMTypeRef field_llvm = field_type ? field_type->llvm_type : LLVMTypeOf(field_val);
            LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, field_llvm, name_node->text);
            LLVMSetAlignment(alloca, LLVMABIAlignmentOfType(ctx->data_layout, field_llvm));
            LLVMBuildStore(ctx->builder, LLVMConstNull(field_llvm), alloca);
            LLVMBuildStore(ctx->builder, field_val, alloca);
            TypedValue tv = create_typed_value(alloca, field_type, true);
            generator_add_symbol(ctx->gen_ctx, name_node->text, tv);
        }
        return struct_val;
    }

    // Single variable declaration
    odin_grammar_node_t * name_node = id_list->list.children[0];
    if (name_node == NULL || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    LLVMTypeRef llvm_type = var_type->llvm_type;
    LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, llvm_type, name_node->text);
    unsigned alignment = LLVMABIAlignmentOfType(ctx->data_layout, llvm_type);
    if (var_type->kind == TD_KIND_STRUCT && var_type->struct_metadata.alignment > alignment)
        alignment = var_type->struct_metadata.alignment;
    LLVMSetAlignment(alloca, alignment);

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
            // If the initializer produced a pointer but the target type is
            // a non-pointer value type (e.g., subscript on multi-pointer or
            // array returns an lvalue pointer), load through the pointer
            if (init_node != NULL && init_node->resolved_type != NULL
                && LLVMGetTypeKind(init_llvm_type) == LLVMPointerTypeKind)
            {
                LLVMTypeRef expected_type = init_node->resolved_type->llvm_type;
                if (expected_type != NULL && LLVMGetTypeKind(expected_type) != LLVMPointerTypeKind)
                {
                    init_val = LLVMBuildLoad2(ctx->builder, expected_type, init_val, "loadtmp");
                    init_llvm_type = LLVMTypeOf(init_val);
                }
            }
            // Auto-convert string struct {ptr, i64} to cstring ptr
            if (var_type && var_type->kind == TD_KIND_BASIC && var_type->as.basic.name != NULL
                && strcmp(var_type->as.basic.name, "cstring") == 0
                && LLVMGetTypeKind(init_llvm_type) == LLVMStructTypeKind)
            {
                init_val = LLVMBuildExtractValue(ctx->builder, init_val, 0, "str2cstr");
            }
            // Auto-convert variable initializer to match declared type
            if (var_type && init_llvm_type != var_type->llvm_type)
            {
                TypeDescriptor const * init_type = init_node->resolved_type;
                bool src_unsigned = (init_type && init_type->kind == TD_KIND_BASIC)
                                    ? init_type->as.basic.is_unsigned : false;
                init_val = coerce_value_to_type(ctx, init_val, var_type->llvm_type, src_unsigned, "varinit");
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
            else if (var_type && var_type->kind == TD_KIND_BASIC && var_type->as.basic.name && strcmp(var_type->as.basic.name, "any") == 0)
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
