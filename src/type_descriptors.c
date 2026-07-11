#include "type_descriptors.h"
#include "hash.h"

#include <stdio.h>
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

    TypeDescriptor * context_type;
    TypeDescriptor * allocator_type;
    TypeDescriptor * source_location_type;
};

static TypeDescriptor *
type_descriptor_alloc(TypeDescriptors * registry)
{
    if (registry->count >= MAX_TYPES)
        return NULL;
    TypeDescriptor * td = calloc(1, sizeof(*td));
    if (td == NULL)
        return NULL;
    td->type_id = 0;
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
        {"i128", 128, false, false},
        {"u8", 8, false, true},
        {"u16", 16, false, true},
        {"u32", 32, false, true},
        {"u64", 64, false, true},
        {"u128", 128, false, true},
        {"f16", 16, true, false},
        {"f32", 32, true, false},
        {"f64", 64, true, false},
        {"rune", 32, false, true},
        {"byte", 8, false, true},
        {"uintptr", 64, false, true},
        {"typeid", 64, false, true},
        {"bool", 1, false, true},
        // Endian-specific integer types (same LLVM types as native on x86_64 LE)
        {"i16le", 16, false, false},
        {"i32le", 32, false, false},
        {"i64le", 64, false, false},
        {"i16be", 16, false, false},
        {"i32be", 32, false, false},
        {"i64be", 64, false, false},
        {"u16le", 16, false, true},
        {"u32le", 32, false, true},
        {"u64le", 64, false, true},
        {"u16be", 16, false, true},
        {"u32be", 32, false, true},
        {"u64be", 64, false, true},
        // Endian-specific float types (same LLVM types on x86_64)
        {"f16le", 16, true, false},
        {"f32le", 32, true, false},
        {"f64le", 64, true, false},
        {"f16be", 16, true, false},
        {"f32be", 32, true, false},
        {"f64be", 64, true, false},
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
                = basic_specs[i].width == 32 ? LLVMFloatTypeInContext(context)
                : basic_specs[i].width == 16 ? LLVMHalfTypeInContext(context)
                : LLVMDoubleTypeInContext(context);
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

    // Complex types: struct { float, float }
    {
        LLVMTypeRef f16_t = LLVMHalfTypeInContext(context);
        LLVMTypeRef f32_t = LLVMFloatTypeInContext(context);
        LLVMTypeRef f64_t = LLVMDoubleTypeInContext(context);

        LLVMTypeRef complex32_fields[2] = {f16_t, f16_t};
        LLVMTypeRef complex64_fields[2] = {f32_t, f32_t};
        LLVMTypeRef complex128_fields[2] = {f64_t, f64_t};

        LLVMTypeRef quat64_fields[4] = {f16_t, f16_t, f16_t, f16_t};
        LLVMTypeRef quat128_fields[4] = {f32_t, f32_t, f32_t, f32_t};
        LLVMTypeRef quat256_fields[4] = {f64_t, f64_t, f64_t, f64_t};

        TypeDescriptor * td;

        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, complex32_fields, 2, false);
            td->as.basic.name = "complex32"; td->as.basic.width = 32;
            reg->basic_types[reg->basic_count++] = td;
        }
        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, complex64_fields, 2, false);
            td->as.basic.name = "complex64"; td->as.basic.width = 64;
            reg->basic_types[reg->basic_count++] = td;
        }
        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, complex128_fields, 2, false);
            td->as.basic.name = "complex128"; td->as.basic.width = 128;
            reg->basic_types[reg->basic_count++] = td;
        }
        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, quat64_fields, 4, false);
            td->as.basic.name = "quaternion64"; td->as.basic.width = 64;
            reg->basic_types[reg->basic_count++] = td;
        }
        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, quat128_fields, 4, false);
            td->as.basic.name = "quaternion128"; td->as.basic.width = 128;
            reg->basic_types[reg->basic_count++] = td;
        }
        td = type_descriptor_alloc(reg);
        if (td) {
            td->kind = TD_KIND_BASIC;
            td->llvm_type = LLVMStructTypeInContext(context, quat256_fields, 4, false);
            td->as.basic.name = "quaternion256"; td->as.basic.width = 256;
            reg->basic_types[reg->basic_count++] = td;
        }
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

    // Compute hashes for all types created during registry initialization
    for (int i = 0; i < reg->count; i++)
    {
        type_compute_hash(reg->types[i]);
    }

    return reg;
}

static void
type_write_canonical_name_internal(TypeDescriptor const * td, char * buf, size_t buf_size, int depth)
{
    if (td == NULL || buf_size == 0)
    {
        if (buf_size > 0) buf[0] = '\0';
        return;
    }
    if (depth > 32)
    {
        snprintf(buf, buf_size, "...");
        return;
    }

    switch (td->kind)
    {
    case TD_KIND_BASIC:
        snprintf(buf, buf_size, "%s", td->as.basic.name ? td->as.basic.name : "?");
        break;

    case TD_KIND_POINTER:
    {
        char inner[512];
        type_write_canonical_name_internal(td->pointee, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "^%s", inner);
        break;
    }

    case TD_KIND_SLICE:
    {
        char inner[512];
        type_write_canonical_name_internal(td->element_type, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "[]%s", inner);
        break;
    }

    case TD_KIND_ARRAY:
    {
        char inner[512];
        type_write_canonical_name_internal(td->element_type, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "[%zu]%s", td->as.array.count, inner);
        break;
    }

    case TD_KIND_DYNAMIC_ARRAY:
    {
        char inner[512];
        type_write_canonical_name_internal(td->element_type, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "[dynamic]%s", inner);
        break;
    }

    case TD_KIND_PROC:
    {
        size_t pos = 0;
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "proc(");
        for (int i = 0; i < td->proc_metadata.param_count; i++)
        {
            if (i > 0)
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, ", ");
            char pbuf[256];
            type_write_canonical_name_internal(td->proc_metadata.params[i], pbuf, sizeof(pbuf), depth + 1);
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", pbuf);
        }
        if (td->proc_metadata.is_variadic)
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, ", ..any");
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, ")");
        if (td->proc_metadata.return_type && td->proc_metadata.return_type != td->proc_metadata.return_type)
        {
            // placeholder — skip for now
        }
        else if (td->proc_metadata.return_count > 0)
        {
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, " -> (");
            for (int i = 0; i < td->proc_metadata.return_count; i++)
            {
                if (i > 0) pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, ", ");
                char rbuf[256];
                type_write_canonical_name_internal(
                    td->proc_metadata.returns[i], rbuf, sizeof(rbuf), depth + 1
                );
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", rbuf);
            }
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, ")");
        }
        else if (!td->proc_metadata.is_void_return && td->proc_metadata.return_type)
        {
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, " -> ");
            char rbuf[256];
            type_write_canonical_name_internal(td->proc_metadata.return_type, rbuf, sizeof(rbuf), depth + 1);
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", rbuf);
        }
        break;
    }

    case TD_KIND_STRUCT:
    {
        size_t pos = 0;
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "struct{");
        for (int i = 0; i < td->struct_metadata.members.count; i++)
        {
            if (i > 0)
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "; ");
            struct_field_t const * f = &td->struct_metadata.members.fields[i];
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s:", f->name ? f->name : "?");
            char fbuf[256];
            type_write_canonical_name_internal(f->type_desc, fbuf, sizeof(fbuf), depth + 1);
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", fbuf);
        }
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "}");
        break;
    }

    case TD_KIND_UNION:
    {
        size_t pos = 0;
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "union{");
        for (int i = 0; i < td->union_metadata.members.count; i++)
        {
            if (i > 0)
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "; ");
            struct_field_t const * f = &td->union_metadata.members.fields[i];
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s:", f->name ? f->name : "?");
            char fbuf[256];
            type_write_canonical_name_internal(f->type_desc, fbuf, sizeof(fbuf), depth + 1);
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", fbuf);
        }
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "}");
        break;
    }

    case TD_KIND_ENUM:
    {
        char const * tag = td->as.enum_type.tag ? td->as.enum_type.tag : "?";
        snprintf(buf, buf_size, "enum(%s)", tag);
        break;
    }

    case TD_KIND_MAP:
    {
        char kbuf[256], vbuf[256];
        type_write_canonical_name_internal(td->as.map.key_type, kbuf, sizeof(kbuf), depth + 1);
        type_write_canonical_name_internal(td->as.map.value_type, vbuf, sizeof(vbuf), depth + 1);
        snprintf(buf, buf_size, "map[%s]%s", kbuf, vbuf);
        break;
    }

    case TD_KIND_BIT_SET:
    {
        char ebuf[256];
        type_write_canonical_name_internal(td->as.bit_set.element_type, ebuf, sizeof(ebuf), depth + 1);
        snprintf(buf, buf_size, "bit_set(%s,%d)", ebuf, td->as.bit_set.num_bits);
        break;
    }

    case TD_KIND_BIT_FIELD:
    {
        size_t pos = 0;
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "bit_field{");
        for (int i = 0; i < td->as.bit_field.num_fields; i++)
        {
            if (i > 0)
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "; ");
            bit_field_field_info const * f = &td->as.bit_field.fields[i];
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s:", f->name ? f->name : "?");
            if (f->type)
            {
                char fbuf[256];
                type_write_canonical_name_internal(f->type, fbuf, sizeof(fbuf), depth + 1);
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", fbuf);
            }
            pos += snprintf(
                buf + pos, buf_size > pos ? buf_size - pos : 0, "|%d:%d", f->offset_bits, f->width_bits
            );
        }
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "}");
        break;
    }

    case TD_KIND_RANGE:
    {
        snprintf(buf, buf_size, "range(%s)", td->as.range.is_inclusive ? "inclusive" : "exclusive");
        break;
    }

    case TD_KIND_SOA:
    {
        size_t pos = 0;
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "#soa[");
        for (int i = 0; i < td->struct_metadata.members.count; i++)
        {
            if (i > 0)
                pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "; ");
            struct_field_t const * f = &td->struct_metadata.members.fields[i];
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s:", f->name ? f->name : "?");
            char fbuf[256];
            type_write_canonical_name_internal(f->type_desc, fbuf, sizeof(fbuf), depth + 1);
            pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "%s", fbuf);
        }
        pos += snprintf(buf + pos, buf_size > pos ? buf_size - pos : 0, "]");
        break;
    }

    case TD_KIND_DISTINCT:
    {
        char inner[512];
        type_write_canonical_name_internal(td->distinct_base_type, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "distinct %s", inner);
        break;
    }

    case TD_KIND_MAYBE:
    {
        char inner[512];
        type_write_canonical_name_internal(td->as.maybe.inner_type, inner, sizeof(inner), depth + 1);
        snprintf(buf, buf_size, "Maybe(%s)", inner);
        break;
    }

    default:
        snprintf(buf, buf_size, "?");
        break;
    }
}

void
type_write_canonical_name(TypeDescriptor const * td, char * buf, size_t buf_size)
{
    type_write_canonical_name_internal(td, buf, buf_size, 0);
}

void
type_compute_hash(TypeDescriptor * td)
{
    if (td == NULL || td->type_id != 0)
        return;

    char buf[2048];
    type_write_canonical_name(td, buf, sizeof(buf));
    td->type_id = (int64_t)hash_string(buf, strlen(buf));
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
        if (registry->types[i]->union_metadata.members.fields)
        {
            free(registry->types[i]->union_metadata.members.fields);
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

void
register_builtin_context_types(TypeDescriptors * registry)
{
    // Allocator struct: { procedure: rawptr, data: rawptr }
    LLVMTypeRef allocator_fields[2];
    allocator_fields[0] = registry->ptr_type->llvm_type;
    allocator_fields[1] = registry->ptr_type->llvm_type;
    LLVMTypeRef allocator_llvm = LLVMStructTypeInContext(registry->context, allocator_fields, 2, false);

    struct_or_union_members_st allocator_members;
    allocator_members.count = 2;
    allocator_members.fields = malloc(2 * sizeof(struct_field_t));
    allocator_members.fields[0].name = "procedure";
    allocator_members.fields[0].type_desc = registry->ptr_type;
    allocator_members.fields[0].is_using = false;
    allocator_members.fields[1].name = "data";
    allocator_members.fields[1].type_desc = registry->ptr_type;
    allocator_members.fields[1].is_using = false;

    registry->allocator_type
        = (TypeDescriptor *)register_struct_type(registry, allocator_llvm, true, &allocator_members);
    free(allocator_members.fields);

    // Context struct (minimal, 4 fields):
    // { allocator: Allocator, temp_allocator: Allocator, user_ptr: rawptr, user_index: int }
    LLVMTypeRef context_fields[4];
    context_fields[0] = registry->allocator_type->llvm_type;
    context_fields[1] = registry->allocator_type->llvm_type;
    context_fields[2] = registry->ptr_type->llvm_type;
    context_fields[3] = registry->i64_type->llvm_type;
    LLVMTypeRef context_llvm = LLVMStructTypeInContext(registry->context, context_fields, 4, false);

    struct_or_union_members_st context_members;
    context_members.count = 4;
    context_members.fields = malloc(4 * sizeof(struct_field_t));
    context_members.fields[0].name = "allocator";
    context_members.fields[0].type_desc = registry->allocator_type;
    context_members.fields[0].is_using = false;
    context_members.fields[1].name = "temp_allocator";
    context_members.fields[1].type_desc = registry->allocator_type;
    context_members.fields[1].is_using = false;
    context_members.fields[2].name = "user_ptr";
    context_members.fields[2].type_desc = registry->ptr_type;
    context_members.fields[2].is_using = false;
    context_members.fields[3].name = "user_index";
    {
        TypeDescriptor const * int_type = get_basic_type_by_name(registry, "int");
        context_members.fields[3].type_desc = int_type ? int_type : registry->i64_type;
    }
    context_members.fields[3].is_using = false;

    registry->context_type = (TypeDescriptor *)register_struct_type(registry, context_llvm, true, &context_members);
    free(context_members.fields);

    // Source_Location struct: { file: string, line: int, column: int }
    TypeDescriptor const * str_type = get_basic_type_by_name(registry, "string");
    TypeDescriptor const * int_type = get_basic_type_by_name(registry, "int");
    if (str_type == NULL) str_type = registry->ptr_type;
    if (int_type == NULL) int_type = registry->i64_type;

    LLVMTypeRef sl_fields[3];
    sl_fields[0] = str_type->llvm_type;
    sl_fields[1] = int_type->llvm_type;
    sl_fields[2] = int_type->llvm_type;
    LLVMTypeRef sl_llvm = LLVMStructTypeInContext(registry->context, sl_fields, 3, false);

    struct_or_union_members_st sl_members;
    sl_members.count = 3;
    sl_members.fields = malloc(3 * sizeof(struct_field_t));
    sl_members.fields[0].name = "file";
    sl_members.fields[0].type_desc = str_type;
    sl_members.fields[0].is_using = false;
    sl_members.fields[1].name = "line";
    sl_members.fields[1].type_desc = int_type;
    sl_members.fields[1].is_using = false;
    sl_members.fields[2].name = "column";
    sl_members.fields[2].type_desc = int_type;
    sl_members.fields[2].is_using = false;

    registry->source_location_type = (TypeDescriptor *)register_struct_type(registry, sl_llvm, true, &sl_members);
    free(sl_members.fields);
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
    type_compute_hash(td);
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
    type_compute_hash(td);
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
    type_compute_hash(td);
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
    type_compute_hash(td);
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
    type_compute_hash(td);
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

    type_compute_hash(td);
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

    type_compute_hash(td);
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
    bool is_variadic,
    calling_convention_t calling_convention
)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_PROC)
            continue;
        if (t->proc_metadata.is_variadic != is_variadic)
            continue;
        if (t->proc_metadata.param_count != param_count)
            continue;
        if (t->proc_metadata.return_count != return_count)
            continue;
        if (t->proc_metadata.return_type != return_type)
            continue;
        if (t->proc_metadata.calling_convention != calling_convention)
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
        if (match && return_count > 0)
        {
            for (int j = 0; j < return_count; j++)
            {
                if (t->proc_metadata.returns[j] != returns[j])
                {
                    match = false;
                    break;
                }
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
    td->proc_metadata.return_count = return_count;
    td->proc_metadata.is_variadic = is_variadic;
    td->proc_metadata.is_void_return
        = (return_count == 0 && (return_type == NULL || return_type == registry->void_type));
    td->proc_metadata.calling_convention = calling_convention;

    if (param_count > 0)
    {
        td->proc_metadata.params = malloc((size_t)param_count * sizeof(*td->proc_metadata.params));
        memcpy((void *)td->proc_metadata.params, params, (size_t)param_count * sizeof(*td->proc_metadata.params));
    }

    if (return_count > 0)
    {
        td->proc_metadata.returns = malloc((size_t)return_count * sizeof(*td->proc_metadata.returns));
        memcpy((void *)td->proc_metadata.returns, returns, (size_t)return_count * sizeof(*td->proc_metadata.returns));
    }

    bool has_context_param = (calling_convention == CALLING_CONV_ODIN && registry->context_type != NULL);
    int llvm_param_count = param_count + (has_context_param ? 1 : 0);

    LLVMTypeRef * llvm_params = NULL;
    if (llvm_param_count > 0)
    {
        llvm_params = malloc((size_t)llvm_param_count * sizeof(*llvm_params));
        int idx = 0;
        if (has_context_param)
        {
            llvm_params[idx++] = LLVMPointerType(registry->context_type->llvm_type, 0);
        }
        for (int i = 0; i < param_count; i++)
        {
            llvm_params[idx++] = params[i]->llvm_type;
        }
    }

    LLVMTypeRef ret_llvm;
    if (return_count > 1)
    {
        LLVMTypeRef * ret_types = malloc((size_t)return_count * sizeof(*ret_types));
        for (int i = 0; i < return_count; i++)
            ret_types[i] = returns[i]->llvm_type;
        ret_llvm = LLVMStructTypeInContext(registry->context, ret_types, (unsigned)return_count, false);
        free(ret_types);
    }
    else
    {
        ret_llvm = return_type ? return_type->llvm_type : LLVMVoidTypeInContext(registry->context);
    }

    // LLVM variadic only for bare ... (not ..any which uses a slice param)
    bool llvm_variadic = is_variadic
        && (calling_convention == CALLING_CONV_C
            || (param_count > 0 && params[param_count - 1]->kind != TD_KIND_SLICE));
    td->proc_metadata.func_type = LLVMFunctionType(ret_llvm, llvm_params, (unsigned)llvm_param_count, llvm_variadic);
    td->llvm_type = LLVMPointerType(td->proc_metadata.func_type, 0);
    free(llvm_params);

    type_compute_hash(td);
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

    type_compute_hash(td);
    return td;
}

TypeDescriptor const *
get_or_create_union_type(TypeDescriptors * registry, struct_or_union_members_st const * members)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_UNION)
            continue;
        if (t->union_metadata.members.count != members->count)
            continue;
        bool match = true;
        for (int j = 0; j < members->count; j++)
        {
            if (strcmp(t->union_metadata.members.fields[j].name, members->fields[j].name) != 0
                || t->union_metadata.members.fields[j].type_desc != members->fields[j].type_desc)
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
    td->kind = TD_KIND_UNION;
    td->union_metadata.is_complete = true;

    if (members && members->count > 0)
    {
        td->union_metadata.members.fields = malloc((size_t)members->count * sizeof(*td->union_metadata.members.fields));
        memcpy(
            td->union_metadata.members.fields,
            members->fields,
            (size_t)members->count * sizeof(*td->union_metadata.members.fields)
        );
        td->union_metadata.members.count = members->count;

        uint64_t max_size = 0;
        uint32_t max_align = 1;
        if (registry->data_layout)
        {
            for (int j = 0; j < members->count; j++)
            {
                if (members->fields[j].type_desc && members->fields[j].type_desc->llvm_type)
                {
                    uint64_t sz = LLVMABISizeOfType(registry->data_layout, members->fields[j].type_desc->llvm_type);
                    uint32_t al
                        = LLVMABIAlignmentOfType(registry->data_layout, members->fields[j].type_desc->llvm_type);
                    if (sz > max_size)
                        max_size = sz;
                    if (al > max_align)
                        max_align = al;
                }
            }
        }
        if (max_size == 0)
            max_size = 1;

        td->union_metadata.max_field_size = max_size;
        td->union_metadata.max_alignment = max_align;

        LLVMTypeRef i8_type = LLVMInt8TypeInContext(registry->context);
        LLVMTypeRef i64_type = LLVMInt64TypeInContext(registry->context);
        LLVMTypeRef payload_type = LLVMArrayType(i8_type, (unsigned)max_size);
        LLVMTypeRef fields[2] = {i64_type, payload_type};
        td->llvm_type = LLVMStructTypeInContext(registry->context, fields, 2, false);
    }
    else
    {
        td->llvm_type = LLVMStructTypeInContext(registry->context, NULL, 0, false);
    }

    type_compute_hash(td);
    return td;
}

TypeDescriptor const *
get_or_create_soa_type(TypeDescriptors * registry, struct_or_union_members_st const * backing_members)
{
    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_SOA)
            continue;
        if (t->struct_metadata.members.count != backing_members->count)
            continue;
        bool match = true;
        for (int j = 0; j < backing_members->count; j++)
        {
            if (strcmp(t->struct_metadata.members.fields[j].name, backing_members->fields[j].name) != 0
                || t->struct_metadata.members.fields[j].type_desc != backing_members->fields[j].type_desc)
            {
                match = false;
                break;
            }
        }
        if (match)
            return t;
    }

    int count = backing_members->count;
    LLVMTypeRef * llvm_types = malloc((size_t)count * sizeof(LLVMTypeRef));
    for (int j = 0; j < count; j++)
    {
        llvm_types[j] = backing_members->fields[j].type_desc->llvm_type;
    }
    LLVMTypeRef llvm_struct = LLVMStructTypeInContext(registry->context, llvm_types, (unsigned)count, false);
    free(llvm_types);

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_SOA;
    td->llvm_type = llvm_struct;
    td->struct_metadata.is_complete = true;

    if (count > 0 && backing_members->fields)
    {
        td->struct_metadata.members.fields = malloc((size_t)count * sizeof(struct_field_t));
        memcpy(td->struct_metadata.members.fields, backing_members->fields, (size_t)count * sizeof(struct_field_t));
        td->struct_metadata.members.count = count;
    }

    td->as.soa.backing_members.count = count;
    td->as.soa.backing_members.fields = NULL;

    if (registry->data_layout)
    {
        td->struct_metadata.total_size = LLVMABISizeOfType(registry->data_layout, llvm_struct);
        td->struct_metadata.alignment = LLVMABIAlignmentOfType(registry->data_layout, llvm_struct);
    }

    type_compute_hash(td);
    return td;
}

int
type_descriptor_find_union_field_index(TypeDescriptor const * desc, char const * name)
{
    if (desc == NULL || desc->kind != TD_KIND_UNION)
        return -1;
    for (int i = 0; i < desc->union_metadata.members.count; i++)
    {
        if (strcmp(desc->union_metadata.members.fields[i].name, name) == 0)
            return i;
    }
    return -1;
}

struct_field_t const *
type_descriptor_get_union_field(TypeDescriptor const * desc, int index)
{
    if (desc == NULL || desc->kind != TD_KIND_UNION)
        return NULL;
    if (index < 0 || index >= desc->union_metadata.members.count)
        return NULL;
    return &desc->union_metadata.members.fields[index];
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
    type_compute_hash(td);
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
    type_compute_hash(td);
    return td;
}

TypeDescriptor const *
get_or_create_maybe_type(TypeDescriptors * registry, TypeDescriptor const * inner_type)
{
    if (inner_type == NULL)
        return NULL;

    for (int i = 0; i < registry->count; i++)
    {
        TypeDescriptor * t = registry->types[i];
        if (t->kind != TD_KIND_MAYBE)
            continue;
        if (t->as.maybe.inner_type == inner_type)
            return t;
    }

    TypeDescriptor * td = type_descriptor_alloc(registry);
    if (td == NULL)
        return NULL;
    td->kind = TD_KIND_MAYBE;
    td->as.maybe.inner_type = inner_type;

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(registry->context);
    LLVMTypeRef fields[2] = {i64_type, inner_type->llvm_type};
    td->llvm_type = LLVMStructTypeInContext(registry->context, fields, 2, false);

    type_compute_hash(td);
    return td;
}

TypeDescriptor const *
type_descriptor_get_context_type(TypeDescriptors * registry)
{
    return registry->context_type;
}

TypeDescriptor const *
type_descriptor_get_source_location_type(TypeDescriptors * registry)
{
    return registry->source_location_type;
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
    if (desc == NULL || (desc->kind != TD_KIND_STRUCT && desc->kind != TD_KIND_SOA))
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
    if (desc == NULL || (desc->kind != TD_KIND_STRUCT && desc->kind != TD_KIND_SOA))
        return NULL;
    if (index < 0 || index >= desc->struct_metadata.members.count)
        return NULL;
    return &desc->struct_metadata.members.fields[index];
}

bool
type_descriptor_find_struct_field_path(TypeDescriptor const * desc, char const * name, field_access_path_t * path)
{
    if (desc == NULL || (desc->kind != TD_KIND_STRUCT && desc->kind != TD_KIND_SOA))
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
