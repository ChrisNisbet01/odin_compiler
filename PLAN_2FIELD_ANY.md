# Plan: Implement 2-Field any with Runtime Type Introspection

## Goal
- Change `any` from 4 fields to 2 fields: `{data: rawptr, type_id: i64}`
- Implement runtime type lookup via `type_info_of(typeid) -> ^Type_Info`
- Update fmt.odin to print arrays, matrices, vectors using this approach

## Phase 1: 2-Field any Struct

### 1.1 Update type_descriptors.c
- Change any struct from 4 fields to 2 fields
- Update width from 128 to 64 bits

### 1.2 Update ir_gen_assign.c
- Remove element_count and element_type_id storage
- Keep only data and type_id

## Phase 2: Runtime Type Info Intrinsics

### 2.1 Create Type_Info Infrastructure
Create src/type_info.c/h with:
- Type_Info struct definition
- Runtime lookup mechanism

### 2.2 Implement type_info_of Intrinsic
- Signature: `type_info_of(type_id: i64) -> ^Type_Info`
- At runtime: look up type_id in pre-computed global array
- Returns pointer to compile-time Type_Info global

### 2.3 Update Remaining Intrinsics
- `any_type_id`: extract field 1
- `any_data_ptr`: extract field 0
- `array_element`: use runtime pointer arithmetic with type_info
- `matrix_element`: use runtime pointer arithmetic with type_info

## Phase 3: Update fmt.odin

### 3.1 Define Type_Info in Odin
Mirror the C Type_Info struct with proper variant handling.

### 3.2 Extend print_value
Add cases for:
- Arrays (use any_element_count, loop elements)
- Slices (similar to arrays)
- Strings (special case - use cstring loop)
- Matrices (2D iteration with column count from Type_Info)
- Vectors/SIMDs (element count, iterate)

### 3.3 Helper Functions
- Use intrinsics to extract element data
- Recursively call print_value for elements
- Match official Odin's fmt formatting

## Execution Order
1. Update any struct to 2 fields
2. Implement type_info_of runtime lookup
3. Test basic type printing still works
4. Extend fmt.odin with aggregate support
5. Run tests