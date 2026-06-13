#pragma once

#include "struct_bitfield_data.h"

#include <llvm-c/Core.h>
#include <stdbool.h>

typedef struct TypeDescriptor TypeDescriptor;

typedef struct TypedValue
{
    bool is_lvalue;
    LLVMValueRef value;
    TypeDescriptor const * type_info;
    struct_bitfield_data_t bitfield;
} TypedValue;

extern TypedValue const NullTypedValue;

TypedValue create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue);

bool typed_value_switch_to_pointee(TypedValue * tv);
