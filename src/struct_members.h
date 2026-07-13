#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct TypeDescriptor TypeDescriptor;

typedef struct
{
    char const * name;
    TypeDescriptor const * type_desc;
    bool is_using;
    uint32_t user_alignment;
    uint64_t offset;
    unsigned bit_offset;
    unsigned bit_width;
    unsigned storage_index;
} struct_field_t;

typedef struct
{
    struct_field_t * fields;
    int count;
} struct_or_union_members_st;
