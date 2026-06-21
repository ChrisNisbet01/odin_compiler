# Unimplemented / Partially Implemented Core Features

## Recently Completed

### Nested procedures as values
Procedure literals can appear anywhere expressions are allowed (including inside other procs). Nested proc declarations as symbols are handled through `AST_NODE_CONSTANT_DECL` in both the semantic analyser and IR generator. Storing, passing as higher-order arguments, and calling via variable all work. Tests cover basic nesting, cross-calling, higher-order passing, and variable storage.

**Affects**: `src/semantic_analyser.c` line 1063–1087, `src/llvm_ir_generator.c` line 2027–2096, `test/test_nested_proc.odin`

### Procedure parameter types not semantically analysed
`AST_NODE_PROCEDURE_SIGNATURE` resolves parameter types individually (lines 113–198, 716–903). Each parameter's type is resolved via `sem_resolve_type_expr`, stored in the proc type descriptor, and registered into the symbol table. The test `test_proc_params.odin` covers int, bool, float, pointer, and mixed parameter types, including pointer dereference return.

**Affects**: `src/semantic_analyser.c` lines 113–198, 716–903. Test: `test/test_proc_params.odin`.

### `any` type assertion and assignment packing
`x.(T)` type assertions now work for `any` type: semantic analyser resolves the target type, and IR generator extracts the packed value (ptrtoint for integers, bitcast for pointers, load-through for structs/floats). Assignment to `any` variables packs the RHS value. Both `ir_gen_assign_expression` and `ir_gen_assign_statement` handle `any` packing. `string` and `cstring` basic types remain registered and functional.

**Affects**: `src/semantic_analyser.c` POSTFIX_ASSERTION handler, `src/llvm_ir_generator.c` POSTFIX_ASSERTION handler + `ir_gen_pack_any` + assignment packing. Test: `test/test_any_ops.odin`.

### `or_else` / `or_return` expressions
`or_else` provides a fallback value when the LHS is nil/zero: if `lhs != 0`, use `lhs`; otherwise evaluate and use `rhs`. Implemented with conditional branch + phi node. `or_return` short-circuits on nil/zero: if the expression evaluates to zero, returns a zero value of the enclosing function's return type. Both work for integer and pointer types.

**Affects**: `src/semantic_analyser.c` OR_ELSE/OR_RETURN handlers, `src/llvm_ir_generator.c` `ir_gen_or_else_expression`/`ir_gen_or_return_expression`. Test: `tests/test_or_else.odin` (also covers or_return).

### Ternary `cond ? a : b`
Fully implemented with conditional branch to then/else blocks and a phi node selecting the result. Supports integer and pointer condition types; both branches must produce compatible types.

**Affects**: `src/semantic_analyser.c` TERNARY_EXPRESSION handler, `src/llvm_ir_generator.c` `ir_gen_ternary_expression`. Tests: `tests/test_ternary.odin`, `tests/test_ternary2.odin`.

### `enum` type
Enums are parsed and semantically analysed. The semantic analyser creates an enum type descriptor with integer backing type and registers each enumerator as a constant in the current scope. The IR generator processes enum types in variable declarations, re-computes enumerator values, and registers them as LLVM constant integers. Test covers enum constant lookup via `cast(int) B`.

**Affects**: `src/semantic_analyser.c` (AST_NODE_ENUM_TYPE handler in sem_resolve_type_expr, ~line 201), `src/type_descriptors.c/h` (get_or_create_enum_type), `src/llvm_ir_generator.c` (ir_gen_register_enum_enumerators, ir_gen_variable_decl enum type detection). Test: `tests/test_enum.odin`.

### `when` compile-time branch
`when` statements inside procedure bodies are now handled as runtime `if` statements. The semantic analyser processes the condition and body (pushing/poping scopes for compound statements). The IR generator dispatches `AST_NODE_WHEN_STATEMENT` to `ir_gen_if_statement`. Note: `when` is treated as runtime `if`, not compile-time evaluation. Top-level `when` declarations (`AST_NODE_WHEN_DECL`) remain unhandled.

**Affects**: `src/semantic_analyser.c` (AST_NODE_WHEN_STATEMENT case), `src/llvm_ir_generator.c` (AST_NODE_WHEN_STATEMENT case dispatch). Test: `tests/test_when.odin`.

### `distinct` type
`distinct` type expressions now work in variable declarations (e.g. `x: distinct int`). The semantic analyser resolves the inner type and returns its descriptor (treating `distinct` as transparent — no type safety checking). The IR generator uses the base type's LLVM type directly. Note: type alias declarations (`MyType :: distinct int`) do not parse due to grammar limitations (TypePrefix not included in PrimaryExpression).

**Affects**: `src/semantic_analyser.c` (AST_NODE_DISTINCT_TYPE cases in sem_resolve_type_expr and sem_evaluate_expr). Test: `tests/test_distinct.odin`.

### `in` / `not_in` operators
`in` and `not_in` operators now generate LLVM IR for both arrays and slices. The semantic analyser handles all comp expressions generically (no special handling needed). The IR generator's `ir_gen_in_expression` performs a linear search: extracts data pointer and length from the RHS container, iterates element-by-element in a loop, branches to found/notfound blocks, and selects the result via phi node. `not_in` negates the result. Tests cover arrays and slices with found/not-found cases in both `if` conditions and assignments.

**Affects**: `src/llvm_ir_generator.c` (`ir_gen_in_expression`, `ir_gen_binary_expression` OP_IN/OP_NOT_IN cases). Test: `tests/test_in.odin`.

### Range expressions (`..` / `..<`)
Range expressions produce `{i64, i64}` struct values. The grammar parses `a..b` (inclusive) and `a..<b` (half-open) via the `RangeOp = DotDotLt | DotDot` rule (ordering is critical — `DotDotLt` must be tried first to avoid greedy `..` matching). The semantic analyser validates integer operands and creates a range type descriptor. The IR generator builds the struct: for inclusive, high = rhs + 1; for half-open, high = rhs.

**Affects**: `src/type_descriptors.c/h` (`get_or_create_range_type`), `src/odin_grammar.gdl` (RangeOp rule ordering), `src/semantic_analyser.c` (AST_NODE_RANGE_EXPRESSION case), `src/llvm_ir_generator.c` (OP_RANGE/OP_RANGE_HALF in binary expressions).

### For-range loop (`for i in expr { body }`)
For-range loops iterate over range expressions. Grammar rules without `@AST_ACTION` annotations flatten children into the parent (`easy_pc_ast.c:341-360`), so `ForStatement` receives `[Identifier, Expression(range), CompoundStatement]` for `for i in 0..10`. The semantic analyser detects for-range (first child is raw `AST_NODE_IDENTIFIER`, range expression resolves to `TD_KIND_RANGE`) and declares the loop variable as `i64`. The IR generator emits: entry (eval range, extract low/high from struct) → init loop var → cond (cmp < high) → body → inc → cond. Continue target = increment block; break target = end block. Tests cover half-open and inclusive ranges.

**Affects**: `src/semantic_analyser.c` (AST_NODE_FOR_STATEMENT for-range detection + var decl), `src/llvm_ir_generator.c` (`ir_gen_for_statement` for-range codegen). Test: `tests/test_range.odin`.

### For-range loop with index + value (`for i, val in expr { body }`)
Two-variable for-range is now supported. Both `i` and `val` receive the same loop value (for range expressions). The semantic analyser declares all identifier children before the range expression as `i64`. The IR generator allocates separate storage for each, initializes all to `low`, and increments all on each iteration. The condition block compares the first variable against `high`.

**Affects**: `src/semantic_analyser.c` (already handles multiple identifiers), `src/llvm_ir_generator.c` (`ir_gen_for_statement` — array-based loop var collection, per-var allocas). Test: `tests/test_range2.odin`.

### `make`, `new`, `delete` built-in procedures
`make(T, len)` allocates a slice backing array via `malloc` and returns a `[]T` slice struct `{data, len}`. `new(T)` allocates a single `T` via `malloc` and returns a `^T` pointer. `delete(expr)` frees backing memory: for slices it extracts the data pointer from the `{ptr, i64}` struct; for pointers it frees the pointer value directly. Grammar rules follow the `LenExpr` pattern. Semantic analyser validates types (make requires slice, new resolves to pointer). IR generation uses `ir_gen_call_malloc`/`ir_gen_call_free` helpers.

**Bug fix**: `delete(p2)` for `^int` pointers used `ir_gen_node` which auto-derefed the pointer, passing the pointed-to value (33) to `free` instead of the heap address. Fixed by using `ir_gen_lvalue_ptr` in the delete handler to bypass auto-deref and load the raw pointer value. Also fixed `LLVMGetTypeKind(val)` → `LLVMGetTypeKind(LLVMTypeOf(val))` (line 770).

**Affects**: `src/odin_grammar.gdl` (MakeExpr, NewExpr, DeleteExpr rules), `src/semantic_analyser.c` (lines 517-562), `src/llvm_ir_generator.c` (ir_gen_call_malloc/free helpers, LenExpr opaque pointer fix, MakeExpr/NewExpr/DeleteExpr handlers, delete handler lvalue fix). Test: `tests/test_make_new_delete.odin`.

### `map[K]V` type
Maps are fully implemented. `make(map[K]V, cap)` allocates a heap-backed map via `calloc`. The runtime data layout is self-contained (no separate runtime library): header fields (count, capacity, key_size, value_size: each i64) are packed into a single `calloc`'d region, followed by inline entries (occupied byte + key bytes + value bytes per slot). `len(m)` reads count from offset 0; `cap(m)` reads capacity from offset 8. `delete(m)` extracts the data pointer from the map struct and frees it.

**Map subscript read** (`x = m[key]`): Emits a linear scan loop with LLVM IR (loop → body → key-check → found/continue → notfound → merge with phi). For integer keys, the stored key is loaded and compared directly against the lookup key. Missing keys return zero.

**Map subscript write** (`m[key] = val`): Emits an upsert loop with LLVM IR. The lvalue handler performs a linear scan tracking both key matches and the first empty slot. If a matching key is found, the value slot pointer is returned. If no match but an empty slot exists, the entry is claimed (occupied byte = 1, key stored, count incremented) and the value slot pointer is returned. If no empty slot exists (overflow), a temporary stack slot is returned.

**Limitations**: Only integer keys are currently supported (direct integer comparison). Non-integer keys would need `memcmp`/`memcpy` extern declarations. Map growth is not implemented — capacity is fixed at `make` time.

**Affects**: `src/type_descriptors.c/h` (`get_or_create_map_type`, `{key_type, value_type}` in union), `src/semantic_analyser.c` (AST_NODE_MAP_TYPE handler, len/cap/make/subscript validation for maps), `src/llvm_ir_generator.c` (identifier skip list, auto-deref exclusion, `ir_gen_call_calloc` helper, map make/len/cap/delete handlers, map subscript lvalue+rvalue in `ir_gen_lvalue`/`ir_gen_postfix_expression`). Test: `tests/test_map.odin`.

### All 44 tests pass.

### `dynamic_array` (`[dynamic]T`)
Dynamic arrays are now fully implemented. `make([dynamic]T, len)` allocates backing memory via `malloc` and returns a 3-field struct `{T*, i64, i64}` (data, length, capacity). Subscript operations use the same data pointer extraction logic as slices. `len(da)` extracts field 1; `cap(da)` extracts field 2. `delete(da)` extracts the data pointer from field 0 and frees it. Type descriptor factory `get_or_create_dynamic_array_type()` creates the `{T*, i64, i64}` LLVM struct type. Semantic analysis validates dynamic arrays for `make`, `len`, `cap`, `delete`, and subscript operations.

**Affects**: `src/type_descriptors.c/h` (`get_or_create_dynamic_array_type`), `src/semantic_analyser.c` (make/len/cap validation accepts dynamic arrays), `src/llvm_ir_generator.c` (make: 3-field struct with cap; len/cap: field index selection; subscript: data pointer extraction; delete: field 0 extraction; identifier skip list; auto-deref exclusion). Test: `tests/test_dynamic_array.odin`.

## Not Implemented

### Statements
- **`when` declaration** (`AST_NODE_WHEN_DECL`) – Parsed, AST built, no sem/IR. Top-level `when` blocks not yet handled.
- **`foreign` blocks / imports** (`AST_NODE_FOREIGN_BLOCK`, `AST_NODE_FOREIGN_IMPORT`) – Parsed, AST built, no sem/IR.
- **Top-level `using`** (`AST_NODE_USING_DECL`) – Parsed, AST built, no sem/IR.

### Expressions
- **Type assertion `.(Type)` for union types** – Currently only works for `any` type. Union type assertions need RTTI.

### Types
- **`union` type** – Parsed, no sem/IR.
- **`bit_field` type** – Parsed, no sem/IR.
- **`bit_set` type** – Parsed, no sem/IR.
- **`soa` (structure-of-arrays) layout** – Parsed, no sem/IR.
- **`any` type / RTTI** – Registered in type registry with correct `{i8*, i64}` layout. Variable declaration and assignment packing for integers, pointers, and struct/array values work. Type assertion `x.(T)` extracts values back. No runtime type identifiers (type_id always 0) or type switching.

### Procedures & Declarations
- **Polymorphic procedures / monomorphisation** – Generics not implemented. `$T` poly-ident (`AST_NODE_POLY_IDENT`) parsed but not handled.
- **Multiple return values** – Grammar supports named returns and return lists; `AST_NODE_NAMED_RETURN`, `AST_NODE_RETURN_LIST` exist. Sem only extracts first return type. IR only handles single `LLVMBuildRet`. No tuple/destructuring support.
- **Variadic parameters (`..`)** – `AST_NODE_ELLIPSIS` parsed but no variadic call or parameter handling.
- **`where` clauses on procedures** – Parsed, no sem/IR.
- **Implicit context parameter** – No hidden `$context` parameter insertion on procedures.
- **`#assert` directive** – Parsed as `AST_NODE_DIRECTIVE_WITH_ARGS`, silently skipped.
- **`#load` / `#partial` / other directives** – Same, parsed and skipped.
- **`context` built-in** – Keyword reserved, no grammar rules or handling.
- **`auto_cast`** – Keyword reserved, not implemented.
- **Inline `asm`** – Keyword reserved, not implemented.
