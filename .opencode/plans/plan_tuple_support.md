# Implementation Plan: Tuple Support

## Goal
Support tuple types and destructuring so `s1, s2 := soa_unzip(z)` works. Required by `soa_unzip` and multi-return procedures.

## What Is Needed

### 1. `TD_KIND_TUPLE` type descriptor

**File**: `src/type_descriptors.h`
- Add `TD_KIND_TUPLE` enum entry (after `TD_KIND_VECTOR`)
- Add to the type descriptor union:
```c
struct {
    TypeDescriptor const ** element_types;
    int count;
} tuple;
```

**File**: `src/type_descriptors.c`
- Add `get_or_create_tuple_type(registry, element_types, count)`:
  - Dedup by pointer-equality of element types and count
  - `llvm_type = LLVMStructTypeInContext(registry->context, element_types_llvm, count, false)`
  - Compute hash
- Add `TD_KIND_TUPLE` case to `type_descriptor_to_string`
- Add `TD_KIND_TUPLE` to the TD hash computation

### 2. Semantic analyser: create tuples

**File**: `src/semantic_analyser.c`

`soa_unzip` handler (to be added later), and/or multi-return procedure call handlers, need to be able to create tuple types.

### 3. Semantic analyser: destructure tuples in assignment/decl

**File**: `src/semantic_analyser.c`

**`AST_NODE_ASSIGN_STATEMENT` handler** (~line 1180 area):
- After evaluating RHS, check if `rhs_type->kind == TD_KIND_TUPLE`
- If tuple:
  - LHS identifiers are children before the operator
  - Validate count matches tuple element count
  - For each LHS: assign the corresponding tuple element type
- If not tuple: existing logic

**`AST_NODE_VARIABLE_DECL` handler** (short-form `:=`):
- Similar check for tuple RHS

**Key challenge**: The comma in `s1, s2 := expr` is handled by the grammar's comma-chain in `AssignStatement`. The LHS identifiers are comma-separated `SuffixExpression` nodes. Need to collect them properly.

### 4. IR generator: create tuple values

**File**: `src/llvm_ir_generator.c`

**`soa_unzip` handler** (to be added later):
- Evaluate arg (SOA struct)
- Alloca for tuple struct
- For each field: extract data+len, create slice, store in tuple field via GEP
- Load and return tuple

### 5. IR generator: destructure tuples in assignment/decl

**File**: `src/llvm_ir_generator.c`

**`ir_gen_assign_statement`**:
- When RHS value's LLVM type is a struct that corresponds to a tuple:
  - For each LHS: GEP to tuple field, load, store in LHS alloca

**`ir_gen_variable_decl`**:
- When init value's LLVM type is a tuple struct:
  - For each var in IdentifierList: GEP to tuple field, load, store in var alloca

### 6. IR generator: the tuple LLVM value

A tuple is just an LLVM struct type. `ExtractValue` or GEP+Load extracts individual elements.

## Files to change (in order)

| # | File | Change |
|---|------|--------|
| 1 | `src/type_descriptors.h` | `TD_KIND_TUPLE` enum + union member |
| 2 | `src/type_descriptors.c` | `get_or_create_tuple_type()`, to_string, hash |
| 3 | `src/semantic_analyser.c` | Tuple destructuring in assignment + var_decl |
| 4 | `src/llvm_ir_generator.c` | Tuple destructuring in assign + var_decl IR gen |
| 5 | `tests/test_tuple.odin` | Test tuple creation + destructuring |

## Total estimated diff: ~300 lines

## Future use: multi-return procedures
Tuple types also enable multi-return procedure calls. The return type list from `AST_NODE_RETURNS` can be stored as a tuple type. Callers destructure via the same tuple mechanism.
