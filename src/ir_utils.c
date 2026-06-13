#include "ir_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char const *
generate_anon_name(int * counter, char const * prefix)
{
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s.%d", prefix, (*counter)++);
    return buf;
}

uint32_t
get_type_alignment(LLVMTargetDataRef data_layout, TypeDescriptor const * desc)
{
    if (desc == NULL || desc->llvm_type == NULL) return 1;
    return LLVMABIAlignmentOfType(data_layout, desc->llvm_type);
}

LLVMValueRef
aligned_store(LLVMBuilderRef builder, LLVMValueRef val, LLVMValueRef ptr, uint32_t alignment)
{
    LLVMSetAlignment(LLVMBuildStore(builder, val, ptr), alignment);
    return ptr;
}

LLVMValueRef
aligned_load(LLVMBuilderRef builder, LLVMValueRef ptr, uint32_t alignment, char const * name)
{
    LLVMValueRef load = LLVMBuildLoad2(builder, LLVMTypeOf(ptr), ptr, name);
    LLVMSetAlignment(load, alignment);
    return load;
}
