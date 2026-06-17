# Unimplemented / Partially Implemented Core Features

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
- **Type assertion `.(Type)`** (`AST_NODE_POSTFIX_ASSERTION`) – Parsed, AST built, no case in `sem_evaluate_expr` or IR rvalue handler.
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
- **`any` type / RTTI** – Registered in type registry with correct `{i8*, i64}` layout (data pointer + type identifier). Basic variable declarations and packing of integer, pointer, and struct values into `any` work. No runtime type information or extraction support yet.

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

## Partially Implemented

### Chained struct member access with `using`
`using` field promotion and chained struct member access are now fully functional. Both single-level (e.g., `v.x`) and chained access (e.g., `v.inner.x`) work correctly in lvalue and rvalue paths. Tests: `test_using.odin`, `test_chained_member.odin`.

### break/continue do not emit defers
`break` and `continue` statements are parsed and handled in the IR generator but do not emit pending defers before the branch. This means defer statements inside loops/switch cases are not executed on break/continue.

### Procedure parameter types not semantically analysed
`AST_NODE_PROCEDURE_SIGNATURE` resolves the return type correctly, but parameter types are not individually analysed or type-checked. The IR generator registers parameters manually.

**Affects**: `src/semantic_analyser.c` line 107–128.

### Nested procedures as values
Procedure literals can appear anywhere expressions are allowed (including inside other procs), but there is no dedicated support for nested proc *declarations* as symbols; they are treated as constant/proc-value declarations, which works via existing variable/constant handling.

### `string` / `cstring` / `any` basic type registration
These types are registered but partially work. `string` maps to `{i8*, i64}` which is correct for the data pointer + length model. `any` incorrectly reuses the string layout.

**Affects**: `src/type_descriptors.c` line 201–208.
