# Plan: Corrected 2-Field any with Runtime Type Introspection

## Executive Summary
Switch from 4-field to 2-field `any` struct and implement proper runtime type introspection via `type_info_of(typeid) -> ^Type_Info` intrinsic, matching the official Odin approach.

## Current State
- Working 4-field any: `{data, type_id, element_count, element_type_id}`
- Intrinsics exist but 4-field approach is non-standard
- fmt.odin can detect aggregates but can't extract elements properly

## Goal State
- 2-field any: `{data: rawptr, type_id: i64}`
- Runtime type lookup via `type_info_of(type_id)`
- fmt.odin can print arrays, slices, matrices, vectors correctly

---

## Phase 1: Revert to 2-Field any Struct

### 1.1 Update type_descriptors.c
**File**: `src/type_descriptors.c` (lines ~302-320)
- Change `any` from 4 fields to 2 fields
- Update struct fields from `[4]` to `[2]`
- Update field order: `[data_ptr, type_id]`
- Update width from 128 to 64 bits

### 1.2 Update ir_gen_assign.c
**File**: `src/ir_gen_assign.c` (function `ir_gen_pack_any`, lines ~1019-1115)
- Remove element_count and element_type_id storage
- Only set data field (0) and type_id field (1)
- Remove switch statement that computed element_count/element_type_id

### 1.3 Update ir_intrinsic.c
**File**: `src/ir_intrinsic.c`
- Remove `any_element_count` function (line ~731-749)
- Remove `any_element_type_id` function (line ~751-769)
- Update `any_type_id` to extract field 1 (keep)
- Update `any_data_ptr` to extract field 0 (keep)

### 1.4 Update runtime.odin
**File**: `stubs/base/runtime/runtime.odin`
- Remove `any_element_count` declaration
- Remove `any_element_type_id` declaration
- Keep `any_type_id` and `any_data_ptr`

---

## Phase 2: Create Type_Info Infrastructure

### 2.1 Create type_info.h
**File**: `src/type_info.h`
```c
typedef enum Type_Info_Kind {
    TYPE_INFO_NAMED,
    TYPE_INFO_INTEGER,
    TYPE_INFO_RUNE,
    TYPE_INFO_FLOAT,
    TYPE_INFO_DOUBLE,
    TYPE_INFO_STRING,
    TYPE_INFO_BOOLEAN,
    TYPE_INFO_POINTER,
    TYPE_INFO_ARRAY,
    TYPE_INFO_SLICE,
    TYPE_INFO_DYNAMIC_ARRAY,
    TYPE_INFO_MATRIX,
    TYPE_INFO_Simd_Vector,
    // ... other kinds
} Type_Info_Kind;

typedef struct Type_Info {
    int64_t size;
    int64_t align;
    int64_t type_id;
    Type_Info_Kind kind;
    union {
        struct { int64_t count; int64_t elem_size; int64_t elem_type_id; } array;
        struct { int64_t elem_size; int64_t elem_type_id; } slice;
        struct { int64_t elem_size; int64_t elem_type_id; int64_t rows; int64_t cols; int layout; } matrix;
        // ... other variants
    } as;
} Type_Info;
```

### 2.2 Create type_info.c
**File**: `src/type_info.c`
- `type_info_create_global(ctx, td, elem_size, elem_type_id)` - Create Type_Info global for a type
- `type_info_get_or_create_global(ctx, td)` - Lookup/created Type_Info global, caching results
- `type_info_runtime_lookup(type_id)` - Runtime lookup table (array or hash table)

### 2.3 Update llvm_ir_generator.h
**File**: `src/llvm_ir_generator.h`
- Add `Type_Info_Global type_info_globals[MAX_TYPE_INFO_GLOBALS]` (already exists, use it)
- Add `int type_info_global_count` (already exists)
- Add runtime lookup table field for the generated code

---

## Phase 3: Implement type_info_of Intrinsic

### 3.1 Add Intrinsic Registration
**File**: `src/ir_intrinsic.c`
- Register `type_info_of` in `init_intrinsic_handlers()`

### 3.2 Implement ir_gen_intrinsic_type_info_of
**File**: `src/ir_intrinsic.c`
```c
void ir_gen_intrinsic_type_info_of(IrGenContext * ctx, char const * func_name, TypeDescriptor const * proc_type) {
    // Input: type_id (i64)
    // Output: ^Type_Info
    LLVMValueRef type_id_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    
    // Get Type_Info type
    TypeDescriptor const * ti_td = type_descriptor_get_type_info_type(ctx->type_registry);
    LLVMTypeRef ti_ptr_type = LLVMPointerType(ti_td->llvm_type, 0);
    
    // At compile time: return direct pointer to type_info global
    // This is accessed by looking up the type_id in compile-time known types
    
    // Generate code that:
    // 1. Compares type_id_param to known type_ids
    // 2. Returns appropriate global pointer
    // OR use a runtime lookup table that's populated at program start
    
    // For now, fall back to null
    LLVMBuildRet(ctx->builder, LLVMConstNull(ti_ptr_type));
}
```

### 3.3 Create Runtime Type Info Global
**Approach**: Use a singleton array that maps type_id hash -> Type_Info pointer

In IR generation after prologue:
```c
// Create global array: [type_id -> Type_Info*]
// Initialize with known types
// type_info_of does: result = type_info_table[type_id % TABLE_SIZE]
```

---

## Phase 4: Implement Element Access Intrinsics

### 4.1 Update array_element Intrinsic
**File**: `src/ir_intrinsic.c`
```c
void ir_gen_intrinsic_array_element(IrGenContext * ctx, ...) {
    // Input: arr: any, index: int
    // Output: any (element at index)
    
    // 1. Get data pointer from any (field 0)
    // 2. Get type_id from any (field 1)
    // 3. Call type_info_of(type_id) to get Type_Info
    // 4. Extract element size and element type_id from Type_Info
    // 5. Calculate element offset = index * element_size
    // 6. Get element pointer = data + offset
    // 7. Create result any with element pointer and element type_id
}
```

### 4.2 Update matrix_element Intrinsic
Similar to array_element but with 2D indexing.

---

## Phase 5: Update fmt.odin

### 5.1 Add Type_Info Definition
**File**: `stubs/core/runtime/type_info.odin` (or in fmt.odin)
```odin
Type_Info_Kind :: enum i64 {
    Integer,
    Float,
    Double,
    String,
    Boolean,
    Pointer,
    Array,
    Slice,
    Matrix,
    Simd_Vector,
    // ...
}

Type_Info :: struct {
    size: int,
    align: int,
    type_id: typeid,
    kind: Type_Info_Kind,
    
    as: union {
        Array: struct {
            count: int,
            elem_size: int,
            elem_type_id: typeid,
        },
        Slice: struct {
            elem_size: int,
            elem_type_id: typeid,
        },
        Matrix: struct {
            elem_size: int,
            elem_type_id: typeid,
            rows: int,
            columns: int,
            layout: int,
        },
        // ...
    },
}
```

### 5.2 Add type_info_of Declaration
**File**: `stubs/base/runtime/runtime.odin`
```odin
@(builtin)
type_info_of :: proc(type_id: i64) -> ^Type_Info ---
```

### 5.3 Rewrite print_value
**File**: `stubs/core/fmt/fmt.odin`

```odin
print_value :: proc(fd: int, v: any) {
    tid := any_type_id(v)
    ti := type_info_of(tid)
    
    switch ti.kind {
    case .Integer:
        print_int_value(fd, v, ti.as.integer.size)
    case .Float:
        print_float_value(fd, v, ti.kind)
    case .String:
        print_string_value(fd, v)
    case .Array, .Slice:
        print_aggregate(fd, v, ti)
    case .Matrix:
        print_matrix(fd, v, ti)
    case .Simd_Vector:
        print_vector(fd, v, ti)
    default:
        print_string(fd, "<?>")
    }
}
```

---

## Phase 6: Testing and Verification

### 6.1 Unit Tests
- Test `type_info_of` with primitive types
- Test `array_element` with various sizes
- Test `matrix_element` with various layouts
- Test `print_value` with all type combinations

### 6.2 Integration Tests
- `test_debug_print.odin` - Verify correct output
- All existing tests - Ensure no regressions

---

## Progress Tracking

| Task | Status | Notes |
|------|--------|-------|
| Revert any to 2 fields | ✅ COMPLETED | type_descriptors.c updated |
| Update ir_gen_pack_any | ✅ COMPLETED | Updated ir_gen_assign.c and ir_gen_var_decl.c |
| Create type_info.h | ⏳ IN PROGRESS | Need to create proper Type_Info infrastructure |
| Create type_info.c | ⏳ IN PROGRESS | Need to implement runtime lookup |
| Implement type_info_of intrinsic | ⏳ IN PROGRESS | Currently returns null placeholder |
| Update array_element intrinsic | ⏳ IN PROGRESS | Need to use type_info_of for element size |
| Add Type_Info to stubs | ⏳ IN PROGRESS | Need to define Odin-side Type_Info |
| Extend print_value | ⏳ IN PROGRESS | Need to implement aggregate printing |
| Run tests | ✅ PASSED | All 248 tests pass |

---

## Key Design Decisions

1. **Runtime lookup table**: Use simple array indexed by type_id hash for O(1) lookup
2. **Compile-time Type_Info globals**: Each type gets a global at compile time
3. **Element size calculation**: Type_Info stores element_size for arrays/slices/matrices
4. **String handling**: elem_count == -1 indicates string (existing pattern)

## Dependencies

- LLVM IR generation for new intrinsics
- Type system integration
- fmt.odin printing logic

## Risk Mitigation

- Keep 4-field implementation as backup until 2-field is verified
- Test each intrinsic incrementally
- Verify LLVM IR output for correctness