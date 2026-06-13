#pragma once

#include "type_descriptors.h"

#include <llvm-c/Core.h>
#include <stdint.h>

char const * generate_anon_name(int * counter, char const * prefix);

uint32_t get_type_alignment(LLVMTargetDataRef data_layout, TypeDescriptor const * desc);

LLVMValueRef aligned_store(LLVMBuilderRef builder, LLVMValueRef val, LLVMValueRef ptr, uint32_t alignment);

LLVMValueRef aligned_load(LLVMBuilderRef builder, LLVMValueRef ptr, uint32_t alignment, char const * name);
