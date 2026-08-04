# Matrix Flat Layout Implementation Notes

## Status: Partially Working

The flat matrix layout implementation has been taken quite far but has some regressions that need debugging.

### Working:
- Basic matrix declaration and read/write via multi-index subscript `[row, col]`
- Matrix literals `{...}` syntax
- Transpose intrinsic (separate row/col reads/writes)
- Hadamar product intrinsic

### Broken:
- Matrix arithmetic (add, sub, mul) return pointers that aren't being loaded correctly
- Matrix assignment `b := a` stores pointers instead of values
- Multi-index subscript on values from operations doesn't work

### Root Cause:
The issue stems from the interaction between:
1. `ir_gen_matrix_binop` returns `result_ptr` (pointer to alloca)
2. Variable initialization expects a LOAD for composite types
3. The load happens when `init_llvm_type` is a pointer type and `expected_type` is array type
4. But the semantics get confused when the result is stored and then subscripted

### Fix Needed:
Either:
1. Make matrix ops return VALUES (load from alloca) and add special handling for subscript GEP on values, OR
2. Keep ops returning pointers but have variable initialization COPY the data (load from source pointer, store to target)

### Files Modified:
- `src/odin_grammar.gdl` - Added row_major/column_major directives
- `src/type_descriptors.c` - Flat LLVM layout, hash includes layout
- `src/sem_type_resolver.c` - Directive-aware matrix resolution
- `src/type_compute_hash.c` - Hash includes layout
- `src/ir_gen_matrix.h` - New helper header for flat offset computation
- `src/ir_gen_postfix.c` - Matrix intrinsics, subscript GEP
- `src/ir_gen_operator.c` - Matrix arithmetic ops
- `src/llvm_ir_generator.c` - Matrix literal generation