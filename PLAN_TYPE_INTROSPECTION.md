# Plan: Runtime Type Introspection Intrinsics for fmt.print_value

## Problem Statement
`fmt.print_value()` doesn't handle aggregate types (arrays, matrices, vectors), printing `<>?>` as fallback. This causes `@debug` output to fail for these types.

## Required Intrinsics

### 1. `type_info_of(v: any) -> TypeInfo`
Returns runtime type information about an `any` value.

**TypeInfo struct** (exposed at runtime):
```odin
TypeInfo :: struct {
    kind: int,           // TD_KIND_ENUM
    element_count: int,  // For arrays/slices/strings
    type_id: int,        // Unique type identifier
    name: string,        // Optional debug name
}
```

### 2. `array_get(arr: $T, index: int) -> $T`
Access array element by index (for debugging/printing).

### 3. `slice_element(s: []T, i: int) -> T`
Access slice element (similar to array_get).

## Implementation Phases

### Phase 1: Compiler Infrastructure
1. Add `type_info_of` intrinsic to `stubs/core/runtime/runtime.odin` as `@(builtin)`
2. Implement codegen for intrinsic in `ir_gen_runtime_intrinsic_body()`
3. Add LLVM IR generation for TypeInfo struct extraction from `any`

### Phase 2: Runtime Type Info Extraction
The `any` struct layout: `{ void* data, i64 type_id }`
Need to:
1. Look up TypeDescriptor by type_id in type registry
2. Extract kind, element_count, name

### Phase 3: Print Logic Enhancement
Add cases to `print_value` for:
- Arrays: iterate elements, print `[e0, e1, e2]`
- Slices: print like arrays (dynamic size)
- Strings: already handled (but add explicit case)
- Matrices: print as nested arrays `[ [r0c0, r0c1], [r1c0, r1c1] ]`
- Vectors: print as arrays `[e0, e1, e2]`

### Phase 4: Test Cases
Create test files to verify:
- `[3]int` zero-initialized prints `[0, 0, 0]`
- `matrix[2,3]int` prints `[[0, 0, 0], [0, 0, 0]]`
- Nested arrays work correctly
- Slices print correctly (with runtime `len`)

## Files to Modify
- `stubs/core/runtime/runtime.odin` — add `@(builtin)` declarations
- `src/llvm_ir_generator.c` — add intrinsic cases in `ir_gen_runtime_intrinsic_body`
- `stubs/core/fmt/fmt.odin` — add array/matrix/vector printing to `print_value`
- `tests/test_debug_print.odin` — new test file