#pragma once

#include "struct_members.h"
#include "type_kinds.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct TypeDescriptors TypeDescriptors;
typedef struct symbol symbol_t;

typedef enum
{
    CALLING_CONV_ODIN,
    CALLING_CONV_CONTEXTLESS,
    CALLING_CONV_C,
    CALLING_CONV_STDCALL,
    CALLING_CONV_FASTCALL,
    CALLING_CONV_NONE,
} calling_convention_t;

typedef struct
{
    TypeDescriptor const * return_type;
    int param_count;
    TypeDescriptor const ** params;
    int return_count;
    TypeDescriptor const ** returns;
    bool is_variadic;
    bool is_void_return;
    calling_convention_t calling_convention;
    LLVMTypeRef func_type;
} ProcMetadata;

typedef struct
{
    bool is_complete;
    struct_or_union_members_st members;
    uint64_t total_size;
    uint32_t alignment;
} StructMetaData;

typedef struct
{
    bool is_complete;
    struct_or_union_members_st members;
    uint64_t max_field_size;
    uint32_t max_alignment;
} UnionMetaData;

typedef enum
{
    TD_KIND_BASIC,
    TD_KIND_POINTER,
    TD_KIND_ARRAY,
    TD_KIND_SLICE,
    TD_KIND_DYNAMIC_ARRAY,
    TD_KIND_STRUCT,
    TD_KIND_UNION,
    TD_KIND_ENUM,
    TD_KIND_BIT_FIELD,
    TD_KIND_BIT_SET,
    TD_KIND_MAP,
    TD_KIND_PROC,
    TD_KIND_DISTINCT,
    TD_KIND_SOA,
    TD_KIND_RANGE,
    TD_KIND_MAYBE,
    TD_KIND_MULTI_POINTER,
    TD_KIND_OVERLOAD_BUNDLE,
} td_kind_t;

typedef struct
{
    char const * name;
    TypeDescriptor const * type;
    int offset_bits;
    int width_bits;
} bit_field_field_info;

typedef struct
{
    char * link_name;
    bool require_results;
    bool is_private;
    bool is_builtin;
} ProcDeclAttributes;

typedef struct TypeDescriptor
{
    td_kind_t kind;
    int64_t type_id;
    LLVMTypeRef llvm_type;

    TypeDescriptor const * pointee;
    TypeDescriptor const * element_type;
    TypeDescriptor const * distinct_base_type;

    ProcMetadata proc_metadata;
    StructMetaData struct_metadata;
    UnionMetaData union_metadata;

    union
    {
        struct
        {
            char const * name;
            int width;
            bool is_float;
            bool is_unsigned;
        } basic;
        struct
        {
            size_t count;
        } array;
        struct
        {
            char const * tag;
            char const ** enumerator_names;
            long long * enumerator_values;
            int enumerator_count;
        } enum_type;
        struct
        {
            bool is_inclusive;
        } range;
        struct
        {
            TypeDescriptor const * key_type;
            TypeDescriptor const * value_type;
        } map;
        struct
        {
            bit_field_field_info * fields;
            int num_fields;
            int total_bits;
        } bit_field;
        struct
        {
            TypeDescriptor const * element_type;
            int num_bits;
        } bit_set;
        struct
        {
            struct_or_union_members_st backing_members;
        } soa;
        struct
        {
            TypeDescriptor const * inner_type;
        } maybe;
        struct
        {
            int candidate_count;
            TypeDescriptor const ** candidate_types;
            symbol_t ** candidate_symbols;
        } overload_bundle;
    } as;
} TypeDescriptor;

TypeDescriptors *
type_descriptors_create_registry(LLVMContextRef context, LLVMTargetDataRef data_layout, LLVMBuilderRef builder);

void type_descriptors_destroy_registry(TypeDescriptors * registry);

TypeDescriptor const *
get_or_create_basic_type(TypeDescriptors * registry, char const * name, int width, bool is_float, bool is_unsigned);

TypeDescriptor const * get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee);

TypeDescriptor const *
get_or_create_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type, size_t count);

TypeDescriptor const * get_or_create_slice_type(TypeDescriptors * registry, TypeDescriptor const * element_type);
TypeDescriptor const *
get_or_create_dynamic_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type);

TypeDescriptor const * get_or_create_enum_type(TypeDescriptors * registry, char const * tag, LLVMTypeRef llvm_type);

TypeDescriptor const * get_or_create_range_type(TypeDescriptors * registry, bool is_inclusive);
TypeDescriptor const *
get_or_create_map_type(TypeDescriptors * registry, TypeDescriptor const * key_type, TypeDescriptor const * value_type);

TypeDescriptor const *
get_or_create_bit_field_type(TypeDescriptors * registry, bit_field_field_info * fields, int num_fields, int total_bits);

bit_field_field_info const * type_descriptor_find_bit_field_field(TypeDescriptor const * desc, char const * name);

TypeDescriptor const *
get_or_create_bit_set_type(TypeDescriptors * registry, TypeDescriptor const * element_type, int num_bits);

TypeDescriptor const * get_or_create_proc_type(
    TypeDescriptors * registry,
    TypeDescriptor const * return_type,
    TypeDescriptor const ** params,
    int param_count,
    TypeDescriptor const ** returns,
    int return_count,
    bool is_variadic,
    calling_convention_t calling_convention
);

TypeDescriptor const * register_struct_type(
    TypeDescriptors * registry, LLVMTypeRef llvm_struct, bool is_complete, struct_or_union_members_st const * members
);

TypeDescriptor const * get_or_create_union_type(TypeDescriptors * registry, struct_or_union_members_st const * members);

TypeDescriptor const *
get_or_create_soa_type(TypeDescriptors * registry, struct_or_union_members_st const * backing_members);

TypeDescriptor const *
get_or_create_maybe_type(TypeDescriptors * registry, TypeDescriptor const * inner_type);

TypeDescriptor const *
get_or_create_multi_pointer_type(TypeDescriptors * registry, TypeDescriptor const * element_type);

TypeDescriptor const *
get_or_create_overload_bundle_type(
    TypeDescriptors * registry,
    TypeDescriptor const ** candidate_types,
    symbol_t ** candidate_symbols,
    int candidate_count
);

TypeDescriptor const * create_distinct_type(TypeDescriptors * registry, TypeDescriptor const * base_type);

int type_descriptor_find_union_field_index(TypeDescriptor const * desc, char const * name);

struct_field_t const * type_descriptor_get_union_field(TypeDescriptor const * desc, int index);

TypeDescriptor const * get_basic_type_by_name(TypeDescriptors * registry, char const * name);

TypeDescriptor const * type_descriptor_get_void_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_int1_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_int8_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_int32_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_int64_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_float32_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_float64_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_ptr_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_context_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_source_location_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_type_info_type(TypeDescriptors * registry);

TypeDescriptor const * type_descriptor_get_type_info_ptr_type(TypeDescriptors * registry);

bool is_integer_kind(TypeDescriptor const * desc);

bool is_floating_kind(TypeDescriptor const * desc);

int type_descriptor_find_struct_field_index(TypeDescriptor const * desc, char const * name);

struct_field_t const * type_descriptor_get_struct_field(TypeDescriptor const * desc, int index);

#define MAX_FIELD_ACCESS_DEPTH 64

typedef struct
{
    int indices[MAX_FIELD_ACCESS_DEPTH];
    int count;
} field_access_path_t;

bool type_descriptor_find_struct_field_path(TypeDescriptor const * desc, char const * name, field_access_path_t * path);

void register_builtin_context_types(TypeDescriptors * registry);

void type_write_canonical_name(TypeDescriptor const * td, char * buf, size_t buf_size);

void type_compute_hash(TypeDescriptor * td);
