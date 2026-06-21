#include "type_descriptors.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TYPES 1024

struct TypeDescriptors
{
    LLVMContextRef context;
    LLVMTargetDataRef data_layout;
    LLVMBuilderRef builder;

    TypeDescriptor * types[MAX_TYPES];
    int count;

    TypeDescriptor * void_type;
    TypeDescriptor * i1_type;
    TypeDescriptor * i8_type;
    TypeDescriptor * i32_type;
    TypeDescriptor * i64_type;
    TypeDescriptor * f32_type;
    TypeDescriptor * f64_type;
    TypeDescriptor * ptr_type;

    TypeDescriptor * basic_types[64];
    int basic_count;
};

static TypeDescriptor *
type_descriptor_alloc(TypeDescriptors * registry)
{
    if (registry->count >= MAX_TYPES)
        return NULL;
    TypeDescriptor * td = calloc(1, sizeof(*td));
    if (td == NULL)
        return NULL;
    registry->types[registry->count++] = td;
    return td;
}

static LLVMTypeRef
string_llvm_type(LLVMContextRef ctx)
{
    LLVMTypeRef elems[2] = {LLVMPointerType(LLVMInt8TypeInContext(ctx), 0), LLVMInt64TypeInContext(ctx)};
    return LLVMStructTypeInContext(ctx, elems, 2, false);
}

TypeDescriptors *
type_descriptors_create_registry(LLVMContextRef context, LLVMTargetDataRef data_layout, LLVMBuilderRef builder)
{
    TypeDescriptors * reg = calloc(1, sizeof(*reg));
    if (reg == NULL)
        return NULL;

    reg->context = context;
    reg->data_layout = data_layout;
    reg->builder = builder;

    reg->void_type = type_descriptor_alloc(reg);
    if (reg->void_type)
    {
        reg->void_type->kind = TD_KIND_BASIC;
        reg->void_type->llvm_type = LLVMVoidTypeInContext(context);
        reg->void_type->as.basic.name = "void";
        reg->void_type->as.basic.width = 0;
    }

    reg->i1_type = type_descriptor_alloc(reg);
    if (reg->i1_type)
    {
        reg->i1_type->kind = TD_KIND_BASIC;
        reg->i1_type->llvm_type = LLVMInt1TypeInContext(context);
        reg->i1_type->as.basic.name = "bool";
        reg->i1_type->as.basic.width = 1;
    }

    reg->i8_type = type_descriptor_alloc(reg);
    if (reg->i8_type)
    {
        reg->i8_type->kind = TD_KIND_BASIC;
        reg->i8_type->llvm_type = LLVMInt8TypeInContext(context);
        reg->i8_type->as.basic.name = "i8";
        reg->i8_type->as.basic.width = 8;
    }

    reg->i32_type = type_descriptor_alloc(reg);
    if (reg->i32_type)
    {
        reg->i32_type->kind = TD_KIND_BASIC;
        reg->i32_type->llvm_type = LLVMInt32TypeInContext(context);
        reg->i32_type->as.basic.name = "i32";
        reg->i32_type->as.basic.width = 32;
    }

    reg->i64_type = type_descriptor_alloc(reg);
    if (reg->i64_type)
    {
        reg->i64_type->kind = TD_KIND_BASIC;
        reg->i64_type->llvm_type = LLVMInt64TypeInContext(context);
        reg->i64_type->as.basic.name = "i64";
        reg->i64_type->as.basic.width = 64;
    }

    reg->f32_type = type_descriptor_alloc(reg);
    if (reg->f32_type)
    {
        reg->f32_type->kind = TD_KIND_BASIC;
        reg->f32_type->llvm_type = LLVMFloatTypeInContext(context);
        reg->f32_type->as.basic.name = "f32";
        reg->f32_type->as.basic.width = 32;
        reg->f32_type->as.basic.is_float = true;
    }

    reg->f64_type = type_descriptor_alloc(reg);
    if (reg->f64_type)
    {
        reg->f64_type->kind = TD_KIND_BASIC;
        reg->f64_type->llvm_type = LLVMDoubleTypeInContext(context);
        reg->f64_type->as.basic.name = "f64";
        reg->f64_type->as.basic.width = 64;
        reg->f64_type->as.basic.is_float = true;
    }

    reg->ptr_type = type_descriptor_alloc(reg);
    if (reg->ptr_type)
    {
        reg->ptr_type->kind = TD_KIND_BASIC;
        reg->ptr_type->llvm_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
        reg->ptr_type->as.basic.name = "rawptr";
        reg->ptr_type->as.basic.width = 64;
    }

    struct
    {
        char const * name;
        int width;
        bool is_float;
        bool is_unsigned;
    } basic_specs[] = {
        {"int", 64, false, true},
        {"i8", 8, false, false},
        {"i16", 16, false, false},
        {"i32", 32, false, false},
        {"i64", 64, false, false},
        {"u8", 8, false, true},
        {"u16", 16, false, true},
        {"u32", 32, false, true},
        {"u64", 64, false, true},
        {"f32", 32, true, false},
        {"f64", 64, true, false},
        {"rune", 32, false, true},
        {"byte", 8, false, true},
        {"uintptr", 64, false, true},
        {"bool", 1, false, true},
    };

    LLVMTypeRef string_llvm = string_llvm_type(context);

    for (size_t i = 0; i < sizeof(basic_specs) / sizeof(basic_specs[0]); i++)
    {
        TypeDescriptor * td = type_descriptor_alloc(reg);
        if (td == NULL)
            continue;

        td->kind = TD_KIND_BASIC;
        td->as.basic.name = basic_specs[i].name;
        td->as.basic.width = basic_specs[i].width;
        td->as.basic.is_float = basic_specs[i].is_float;
        td->as.basic.is_unsigned = basic_specs[i].is_unsigned;

        if (basic_specs[i].is_float)
        {
            td->llvm_type
                = basic_specs[i].width == 32 ? LLVMFloatTypeInContext(context) : LLVMDoubleTypeInContext(context);
        }
        else
        {
            td->llvm_type = LLVMIntTypeInContext(context, basic_specs[i].width);
        }

        reg->basic_types[reg->basic_count++] = td;
    }

    TypeDescriptor * string_td = type_descriptor_alloc(reg);
    if (string_td)
    {
        string_td->kind = TD_KIND_BASIC;
        string_td->llvm_type = string_llvm;
        string_td->as.basic.name = "string";
        string_td->as.basic.width = 128;
        reg->basic_types[reg->basic_count++] = string_td;
    }

    TypeDescriptor * cstring_td = type_descriptor_alloc(reg);
    if (cstring_td)
    {
        cstring_td->kind = TD_KIND_BASIC;
        cstring_td->llvm_type = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
        cstring_td->as.basic.name = "cstring";
        cstring_td->as.basic.width = 64;
        reg->basic_types[reg->basic_count++] = cstring_td;
    }

    // Define `any` as a struct { i8* data; i64 type_id } to distinguish from `string`
    LLVMTypeRef any_llvm = LLVMStructCreateNamed(context, "any");
    LLVMTypeRef any_fields[2] = {
        LLVMPointerType(LLVMInt8TypeInContext(context), 0), // data pointer
        LLVMInt64TypeInContext(context)                     // type identifier
    };
    LLVMStructSetBody(any_llvm, any_fields, 2, 0);
    TypeDescriptor * any_td = type_descriptor_alloc(reg);
    if (any_td)
    {
        any_td->kind = TD_KIND_BASIC;
        any_td->llvm_type = any_llvm;
        any_td->as.basic.name = "any";
        any_td->as.basic.width = 128;
        reg->basic_types[reg->basic_count++] = any_td;
    }

    return reg;
}

void
type_descriptors_destroy_registry(TypeDescriptors * registry)
{
    if (registry == NULL)
        return;
    for (int i = 0; i < registry->count; i++)
    {
        if (registry->types[i]->struct_metadata.members.fields)
        {
            free(registry->types[i]->struct_metadata.members.fields);
        }
        if (registry->types[i]->proc_metadata.params)
        {
            free((void *)registry->types[i]->proc_metadata.params);
        }
        if (registry->types[i]->proc_metadata.returns)
        {
            free((void *)registry->types[i]->proc_metadata.returns);
        }
        free(registry->types[i]);
    }
    free(registry);
}

TypeDescriptor const *
get_or_create_basic_type(TypeDescriptors * registry, char const * name, int width, bool is_float, bool is_unsigned)
{
    (void)registry;
    (void)name;
    (void)width;
    (void)is_float;
    (void)is_unsigned;
    return get_basic_type_by_name(registry, name);
}

TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_POINTER && t->pointee == pointee)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_POINTER;
    td->pointee = pointee;
    td->llvm_type = LLVMPointerType(pointee->llvm_type, 0);
    return td;
}

TypeDescriptor const *
get_or_create_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type, size_t count)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_ARRAY && t->element_type == element_type && t->as.array.count == count)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_ARRAY;
    td->element_type = element_type;
    td->as.array.count = count;
    td->llvm_type = LLVMArrayType(element_type->llvm_type, (unsigned)count);
    return td;
}

TypeDescriptor const *
get_or_create_slice_type(TypeDescriptors * registry, TypeDescriptor const * element_type)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_SLICE && t->element_type == element_type)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_SLICE;
    td->element_type = element_type;

    LLVMTypeRef elems[2] = {LLVMPointerType(element_type->llvm_type, 0), LLVMInt64TypeInContext(registry->context)};
    td->llvm_type = LLVMStructTypeInContext(registry->context, elems, 2, false);
    return td;
}

TypeDescriptor const *
get_or_create_dynamic_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_DYNAMIC_ARRAY && t->element_type == element_type)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_DYNAMIC_ARRAY;
    td->element_type = element_type;

    LLVMTypeRef elems[3]
        = {LLVMPointerType(element_type->llvm_type, 0),
           LLVMInt64TypeInContext(registry->context),
           LLVMInt64TypeInContext(registry->context)};
    td->llvm_type = LLVMStructTypeInContext(registry->context, elems, 3, false);
    return td;
}

TypeDescriptor const *
get_or_create_map_type(TypeDescriptors * registry, TypeDescriptor const * key_type, TypeDescriptor const * value_type)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_MAP && t->as.map.key_type == key_type && t->as.map.value_type == value_type)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_MAP;
    td->as.map.key_type = key_type;
    td->as.map.value_type = value_type;

    LLVMTypeRef elems[1] = {LLVMPointerType(LLVMInt8TypeInContext(registry->context), 0)};
    td->llvm_type = LLVMStructTypeInContext(registry->context, elems, 1, false);
    return td;
}

TypeDescriptor const *
get_or_create_bit_field_type(TypeDescriptors * registry, bit_field_field_info * fields, int num_fields, int total_bits)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_BIT_FIELD)
            continue;
        if (t->as.bit_field.num_fields != num_fields || t->as.bit_field.total_bits != total_bits)
            continue;
        bool match = true;
        for (int j = 0; j < num_fields; j++)
        {
            if (strcmp(t->as.bit_field.fields[j].name, fields[j].name) != 0)
            {
                match = false;
                break;
            }
            if (t->as.bit_field.fields[j].type != fields[j].type
                || t->as.bit_field.fields[j].offset_bits != fields[j].offset_bits
                || t->as.bit_field.fields[j].width_bits != fields[j].width_bits)
            {
                match = false;
                break;
            }
        }
        if (match)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_BIT_FIELD;

    td->as.bit_field.fields = calloc((size_t)num_fields, sizeof(bit_field_field_info));
    if (td->as.bit_field.fields == NULL)
        return NULL;
    for (int j = 0; j < num_fields; j++)
    {
        td->as.bit_field.fields[j] = fields[j];
    }
    td->as.bit_field.num_fields = num_fields;
    td->as.bit_field.total_bits = total_bits;

    if (total_bits <= 8)
        td->llvm_type = LLVMInt8TypeInContext(registry->context);
    else if (total_bits <= 16)
        td->llvm_type = LLVMInt16TypeInContext(registry->context);
    else if (total_bits <= 32)
        td->llvm_type = LLVMInt32TypeInContext(registry->context);
    else
        td->llvm_type = LLVMInt64TypeInContext(registry->context);

    return td;
}

bit_field_field_info const *
type_descriptor_find_bit_field_field(TypeDescriptor const * desc, char const * name)
{
    if (desc == NULL || desc->kind != TD_KIND_BIT_FIELD || name == NULL)
        return NULL;
    for (int i = 0; i < desc->as.bit_field.num_fields; i++)
    {
        if (strcmp(desc->as.bit_field.fields[i].name, name) == 0)
            return &desc->as.bit_field.fields[i];
    }
    return NULL;
}

TypeDescriptor const *
get_or_create_bit_set_type(TypeDescriptors * registry, TypeDescriptor const * element_type, int num_bits)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_BIT_SET)
            continue;
        if (t->as.bit_set.element_type == element_type && t->as.bit_set.num_bits == num_bits)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_BIT_SET;
    td->as.bit_set.element_type = element_type;
    td->as.bit_set.num_bits = num_bits;

    if (num_bits <= 8)
        td->llvm_type = LLVMInt8TypeInContext(registry->context);
    else if (num_bits <= 16)
        td->llvm_type = LLVMInt16TypeInContext(registry->context);
    else if (num_bits <= 32)
        td->llvm_type = LLVMInt32TypeInContext(registry->context);
    else
        td->llvm_type = LLVMInt64TypeInContext(registry->context);

    return td;
}

TypeDescriptor const *
get_or_create_proc_type(
    TypeDescriptors * registry,
    TypeDescriptor const * return_type,
    TypeDescriptor const ** params,
    int param_count,
    TypeDescriptor const ** returns,
    int return_count,
    bool is_variadic
)
{
    (void)returns;
    (void)return_count;

    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_PROC)
            continue;
        if (t->proc_metadata.is_variadic != is_variadic)
            continue;
        if (t->proc_metadata.param_count != param_count)
            continue;
        if (t->proc_metadata.return_type != return_type)
            continue;
        bool match = true;
        for (int j = 0; j < param_count; j++)
        {
            if (t->proc_metadata.params[j] != params[j])
            {
                match = false;
                break;
            }
        }
        if (match)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_PROC;
    td->proc_metadata.return_type = return_type;
    td->proc_metadata.param_count = param_count;
    td->proc_metadata.is_variadic = is_variadic;
    td->proc_metadata.is_void_return = (return_type == NULL || return_type == registry->void_type);

    if (param_count > 0)
    {
        td->proc_metadata.params = malloc((size_t)param_count * sizeof(*td->proc_metadata.params));
        memcpy((void *)td->proc_metadata.params, params, (size_t)param_count * sizeof(*td->proc_metadata.params));
    }

    LLVMTypeRef * llvm_params = NULL;
    if (param_count > 0)
    {
        llvm_params = malloc((size_t)param_count * sizeof(*llvm_params));
        for (int i = 0; i < param_count; i++)
        {
            llvm_params[i] = params[i]->llvm_type;
        }
    }
    LLVMTypeRef ret_llvm = return_type ? return_type->llvm_type : LLVMVoidTypeInContext(registry->context);
    td->proc_metadata.func_type = LLVMFunctionType(ret_llvm, llvm_params, (unsigned)param_count, is_variadic);
    td->llvm_type = LLVMPointerType(td->proc_metadata.func_type, 0);
    free(llvm_params);

    return td;
}

TypeDescriptor const *
register_struct_type(
    TypeDescriptors * registry, LLVMTypeRef llvm_struct, bool is_complete, struct_or_union_members_st const * members
)
{
    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_STRUCT;
    td->llvm_type = llvm_struct;
    td->struct_metadata.is_complete = is_complete;

    if (members && members->count > 0)
    {
        td->struct_metadata.members.fields
            = malloc((size_t)members->count * sizeof(*td->struct_metadata.members.fields));
        memcpy(
            td->struct_metadata.members.fields,
            members->fields,
            (size_t)members->count * sizeof(*td->struct_metadata.members.fields)
        );
        td->struct_metadata.members.count = members->count;
    }

    if (is_complete && registry->data_layout)
    {
        td->struct_metadata.total_size = LLVMABISizeOfType(registry->data_layout, llvm_struct);
        td->struct_metadata.alignment = LLVMABIAlignmentOfType(registry->data_layout, llvm_struct);
    }

    return td;
}

TypeDescriptor const *
get_or_create_enum_type(TypeDescriptors * registry, char const * tag, LLVMTypeRef llvm_type)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_ENUM)
            continue;
        if (tag != NULL && t->as.enum_type.tag != NULL && strcmp(t->as.enum_type.tag, tag) == 0)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_ENUM;
    td->llvm_type = llvm_type ? llvm_type : LLVMInt32TypeInContext(registry->context);
    if (tag)
        td->as.enum_type.tag = tag;
    return td;
}

TypeDescriptor const *
get_or_create_range_type(TypeDescriptors * registry, bool is_inclusive)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind == TD_KIND_RANGE && t->as.range.is_inclusive == is_inclusive)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_RANGE;
    td->as.range.is_inclusive = is_inclusive;

    LLVMTypeRef elems[2] = {LLVMInt64TypeInContext(registry->context), LLVMInt64TypeInContext(registry->context)};
    td->llvm_type = LLVMStructTypeInContext(registry->context, elems, 2, false);
    return td;
}

TypeDescriptor const *
get_basic_type_by_name(TypeDescriptors * registry, char const * name)
{
    for (int i = 0; i < registry->basic_count; i++)
    {
        if (strcmp(registry->basic_types[i]->as.basic.name, name) == 0)
            return registry->basic_types[i];
    }
    return NULL;
}

TypeDescriptor const *
type_descriptor_get_void_type(TypeDescriptors * registry)
{
    return registry->void_type;
}

TypeDescriptor const *
type_descriptor_get_int1_type(TypeDescriptors * registry)
{
    return registry->i1_type;
}

TypeDescriptor const *
type_descriptor_get_int8_type(TypeDescriptors * registry)
{
    return registry->i8_type;
}

TypeDescriptor const *
type_descriptor_get_int32_type(TypeDescriptors * registry)
{
    return registry->i32_type;
}

TypeDescriptor const *
type_descriptor_get_int64_type(TypeDescriptors * registry)
{
    return registry->i64_type;
}

TypeDescriptor const *
type_descriptor_get_float32_type(TypeDescriptors * registry)
{
    return registry->f32_type;
}

TypeDescriptor const *
type_descriptor_get_float64_type(TypeDescriptors * registry)
{
    return registry->f64_type;
}

TypeDescriptor const *
type_descriptor_get_ptr_type(TypeDescriptors * registry)
{
    return registry->ptr_type;
}

bool
is_integer_kind(TypeDescriptor const * desc)
{
    if (desc == NULL)
        return false;
    if (desc->kind != TD_KIND_BASIC)
        return false;
    return !desc->as.basic.is_float && desc->as.basic.width > 0;
}

bool
is_floating_kind(TypeDescriptor const * desc)
{
    if (desc == NULL)
        return false;
    if (desc->kind != TD_KIND_BASIC)
        return false;
    return desc->as.basic.is_float;
}

int
type_descriptor_find_struct_field_index(TypeDescriptor const * desc, char const * name)
{
    if (desc == NULL || desc->kind != TD_KIND_STRUCT)
        return -1;
    for (int i = 0; i < desc->struct_metadata.members.count; i++)
    {
        if (strcmp(desc->struct_metadata.members.fields[i].name, name) == 0)
            return i;
    }
    return -1;
}

struct_field_t const *
type_descriptor_get_struct_field(TypeDescriptor const * desc, int index)
{
    if (desc == NULL || desc->kind != TD_KIND_STRUCT)
        return NULL;
    if (index < 0 || index >= desc->struct_metadata.members.count)
        return NULL;
    return &desc->struct_metadata.members.fields[index];
}

bool
type_descriptor_find_struct_field_path(TypeDescriptor const * desc, char const * name, field_access_path_t * path)
{
    if (desc == NULL || desc->kind != TD_KIND_STRUCT)
        return false;

    for (int i = 0; i < desc->struct_metadata.members.count; i++)
    {
        if (strcmp(desc->struct_metadata.members.fields[i].name, name) == 0)
        {
            path->indices[0] = i;
            path->count = 1;
            return true;
        }
    }

    for (int i = 0; i < desc->struct_metadata.members.count; i++)
    {
        struct_field_t const * field = &desc->struct_metadata.members.fields[i];
        if (!field->is_using)
            continue;
        if (field->type_desc == NULL || field->type_desc->kind != TD_KIND_STRUCT)
            continue;

        field_access_path_t sub_path;
        if (type_descriptor_find_struct_field_path(field->type_desc, name, &sub_path))
        {
            path->indices[0] = i;
            int copy_count = sub_path.count;
            if (copy_count > MAX_FIELD_ACCESS_DEPTH - 1)
                copy_count = MAX_FIELD_ACCESS_DEPTH - 1;
            for (int j = 0; j < copy_count; j++)
            {
                path->indices[j + 1] = sub_path.indices[j];
            }
            path->count = sub_path.count + 1;
            return true;
        }
    }

    return false;
}
