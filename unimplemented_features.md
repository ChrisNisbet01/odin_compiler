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

## Not Implemented

### Statements
- **`when` compile-time branch** (`AST_NODE_WHEN_STATEMENT`) – Parsed, AST built, but no case in semantic analyser or IR generator. Silently skipped.
- **`when` declaration** (`AST_NODE_WHEN_DECL`) – Same as above; no sem/IR handling.
- **`foreign` blocks / imports** (`AST_NODE_FOREIGN_BLOCK`, `AST_NODE_FOREIGN_IMPORT`) – Parsed, AST built, no sem/IR.
- **Top-level `using`** (`AST_NODE_USING_DECL`) – Parsed, AST built, no sem/IR.

### Expressions
- **`or_else`** (`AST_NODE_OR_ELSE`) – Parsed, AST built, sem and IR both treat it as identity on the LHS. The fallback expression is never evaluated. No nil/error checking.
- **`or_return`** (`AST_NODE_OR_RETURN`) – Same as above; treated as identity. No return-on-nil/error behavior.
- **Ternary `cond ? a : b`** (`AST_NODE_TERNARY_EXPRESSION`) – Parsed, AST built, sem and IR both evaluate/return only the condition. `a` and `b` branches ignored.
- **Type assertion `.(Type)`** (`AST_NODE_POSTFIX_ASSERTION`) – Now supported for `any` type: semantic analyser resolves target type, IR generator extracts packed value via ptrtoint/bitcast/load. Not yet supported for union types.
- **`in` / `not_in` operators** – Parsed, operator metadata stored, but `ir_gen_binary_op_by_kind` returns NULL for these (no IR codegen).
- **Range `..` / `..<` in expressions** – Parsed, operator metadata stored, no IR codegen. For-range loops not implemented either.
- **For-range clause** (`for i, val in expr`) – Grammar parses it, but sem and IR treat it as C-style for. Range iteration not implemented.
- **Built-in procedures** (`make`, `new`, `delete`) – Keywords defined and reserved, no grammar rules or AST nodes.

### Types
- **`dynamic_array`** (`[dynamic]T`) – Parsed, no sem/IR.
- **`map` type** – Parsed, no sem/IR.
- **`union` type** – Parsed, no sem/IR.
- **`enum` type** – Parsed, no sem/IR.
- **`bit_field` type** – Parsed, no sem/IR.
- **`bit_set` type** – Parsed, no sem/IR.
- **`soa` (structure-of-arrays) layout** – Parsed, no sem/IR.
- **`distinct` type** – Parsed, no sem/IR.
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
