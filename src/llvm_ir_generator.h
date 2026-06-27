#pragma once

#include "generator_lists.h"
#include "ir_gen_error.h"
#include "odin_grammar_ast.h"
#include "type_descriptors.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

#define MAX_LOOP_DEPTH 64
#define MAX_FUNC_DEPTH 64
#define MAX_DEFERS 128

typedef struct
{
    odin_grammar_node_t * node;
    int scope_depth;
} DeferEntry;

typedef struct
{
    LLVMBasicBlockRef continue_bb;
    LLVMBasicBlockRef break_bb;
    int scope_depth;
} LoopContext;

typedef struct
{
    LLVMValueRef function;
    TypeDescriptor const * return_type;
} FuncContext;

typedef struct
{
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMTargetDataRef data_layout;
    LLVMBuilderRef builder;

    TypeDescriptors * type_registry;
    GeneratorContext * gen_ctx;

    FuncContext func_stack[MAX_FUNC_DEPTH];
    int func_depth;

    IrGenErrorCollection errors;

    int anon_counter;

    LoopContext loop_stack[MAX_LOOP_DEPTH];
    int loop_depth;

    LLVMBasicBlockRef fallthrough_target_bb;

    DeferEntry defer_stack[MAX_DEFERS];
    int defer_count;
    int current_scope_depth;

    LLVMTypeRef auto_cast_target_type;

    char ** foreign_libraries;
    int foreign_library_count;
    int foreign_library_capacity;
} IrGenContext;

IrGenContext *
ir_gen_context_create(char const * module_name, TypeDescriptors * type_registry, GeneratorContext * gen_ctx);

void ir_gen_context_destroy(IrGenContext * ctx);

bool ir_generate(IrGenContext * ctx, odin_grammar_node_t * ast);

int write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path);

int emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type);
