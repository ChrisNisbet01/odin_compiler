#pragma once

#include <llvm-c/Core.h>
#include <stdint.h>

void type_info_table_init(void);
void type_info_table_insert(int64_t type_id, LLVMValueRef type_info_global);
LLVMValueRef type_info_table_lookup(int64_t type_id);
void type_info_destroy(void);