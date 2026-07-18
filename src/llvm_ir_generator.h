#pragma once

#include "generator_lists.h"
#include "ir_gen_error.h"
#include "odin_grammar_ast.h"
#include "package_resolver.h"
#include "type_descriptors.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

#define MAX_LOOP_DEPTH 64
#define MAX_FUNC_DEPTH 64
#define MAX_DEFERS 128
#define MAX_TYPE_INFO_GLOBALS 256

typedef struct
{
    int64_t type_id;
    LLVMValueRef global;
} TypeInfoGlobalEntry;

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

    char const * file_path;

    char ** foreign_libraries;
    int foreign_library_count;
    int foreign_library_capacity;

    ImportedPackage ** imports;
    int import_count;

    LLVMValueRef odin_argc_global;
    LLVMValueRef odin_argv_global;

    TypeInfoGlobalEntry type_info_globals[MAX_TYPE_INFO_GLOBALS];
    int type_info_global_count;

    bool bounds_checking_enabled;
} IrGenContext;

IrGenContext *
ir_gen_context_create(char const * module_name, TypeDescriptors * type_registry, GeneratorContext * gen_ctx);

void ir_gen_context_destroy(IrGenContext * ctx);

bool ir_generate(IrGenContext * ctx, odin_grammar_node_t * ast);

int write_llvm_ir_to_file(LLVMModuleRef module, char const * file_path);

int emit_to_file(LLVMModuleRef module, char const * file_path, char const * march, LLVMCodeGenFileType file_type);

// Shared IR gen helpers (used across extracted modules)
void func_push(IrGenContext * ctx, LLVMValueRef func, TypeDescriptor const * return_type);
void func_pop(IrGenContext * ctx);
LLVMValueRef func_current_function(IrGenContext * ctx);
void ir_gen_implicit_return(IrGenContext * ctx);
void ir_gen_register_enum_enumerators(IrGenContext * ctx, odin_grammar_node_t * enum_type_node);
LLVMValueRef ir_gen_node(IrGenContext * ctx, odin_grammar_node_t * node);
bool ir_gen_node_contains_auto_cast(odin_grammar_node_t * node);
LLVMValueRef coerce_value_to_type(IrGenContext * ctx, LLVMValueRef value, LLVMTypeRef target_type,
                                   bool src_is_unsigned, char const * name_hint);
LLVMValueRef ir_gen_lvalue(IrGenContext * ctx, odin_grammar_node_t * node);
LLVMValueRef ir_gen_lvalue_ptr(IrGenContext * ctx, odin_grammar_node_t * node);
odin_grammar_node_t * expression_unwrap_to_identifier(odin_grammar_node_t * node);
bool is_expression_wrapper_type(odin_grammar_node_type_t type);
LLVMValueRef ir_gen_emit_bounds_check(IrGenContext * ctx, LLVMValueRef index_val,
                                      LLVMValueRef len_val, odin_grammar_node_t * node);
LLVMValueRef ir_gen_map_subscript(IrGenContext * ctx, LLVMValueRef map_val,
                                  TypeDescriptor const * map_type,
                                  LLVMValueRef index_val,
                                  TypeDescriptor const ** out_val_type,
                                  bool is_lvalue, char const * prefix);

// Postfix expression codegen
LLVMValueRef ir_gen_postfix_expression(IrGenContext * ctx, odin_grammar_node_t * node);
void ir_gen_collect_comma_chain_args(odin_grammar_node_t * node, odin_grammar_node_t ** out_args,
                                     int max_args, int * out_count);

bool ir_gen_is_dereferenceable(TypeDescriptor const * td);

// Internal helpers used by postfix codegen
void ir_gen_pack_any(IrGenContext * ctx, LLVMValueRef lhs_ptr, LLVMValueRef rhs_val,
                     LLVMTypeRef any_struct_type, TypeDescriptor const * source_type);
bool ir_gen_resolve_aggregate_field(IrGenContext * ctx, TypeDescriptor const * agg_type,
                                    char const * field_name, LLVMTypeRef * out_struct_type,
                                    int * out_field_idx, TypeDescriptor const ** out_field_type,
                                    char const ** out_error_name);
