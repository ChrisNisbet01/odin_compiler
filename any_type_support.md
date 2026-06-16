# Missing/Incomplete 'any' Type Support

## Current Status
- [x] Type registration in `type_descriptors.c` - `any` is registered as `{i8*, i64}`
- [x] Basic variable declaration parsing

## Missing/Incomplete Functionality

### 1. IR Generation for 'any' Values
**Location:** `src/llvm_ir_generator.c`

When storing an `any` value, the code currently treats it as `i64` instead of the proper `%any` struct type. The store instruction should create a struct with both data pointer and type_id.

**Example:**
```odin
a: any = 0
```
Currently generates: `store i64 0, ptr %a`
Should generate: struct creation with data=0, type_id=??? then `store %any, ptr %a`

### 2. Type Assignment to 'any'
**Location:** `src/semantic_analyser.c` - `sem_evaluate_expr`

When assigning a value to an `any` variable, the semantic analyzer should:
- Accept any type as valid for `any`
- Potentially set a type_id for runtime type information

### 3. Type Resolution for 'any' Literals
**Location:** `src/semantic_analyser.c`

Need to handle `any` type for:
- Integer literals assigned to `any`
- String literals assigned to `any`
- Other basic types

### 4. IR Generation for 'any' from Different Types
**Location:** `src/llvm_ir_generator.c`

Need functions to:
- Convert `int` to `any` (create struct with data pointer + type_id)
- Convert `string` to `any`
- Extract data from `any` back to original type (transmute)

## Test Cases Needed

1. Basic variable declaration with `any` type
2. Assignment of integer to `any`
3. Assignment of string to `any`
4. Return `any` from procedure
5. Pass `any` as parameter
6. Cast/transmute between `any` and other types

## Notes
- The `any` type is currently defined as `{i8* data; i64 type_id}` in LLVM IR
- The `type_id` field is intended for future runtime type information support