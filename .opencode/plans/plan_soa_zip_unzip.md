# Implementation Plan: `soa_zip` / `soa_unzip`

## Context
Our SOA struct fields are slices (`[]T`), not multi-pointers (`[^]T`) like official Odin. This changes the semantics slightly but is simpler.

## `soa_zip(slice1, slice2, ...)`

**Semantics**: Takes 1+ slice args (mixed element types allowed). Returns a `struct #soa` with slice fields truncated to min length.

**Grammar** (`odin_grammar.gdl`):
- Add `KwSoaZip = lexeme("soa_zip" IdBoundary);` to `AllReservedWords`
- `SoaZipExpr = KwSoaZip LParen ArgumentList RParen @AST_ACTION_SOA_ZIP_EXPR;`
- Add to `UnaryExpression` alternatives

**AST**: `AST_NODE_SOA_ZIP_EXPR`

**Semantic analyser**:
- Scan children for args
- Each arg: eval, validate `resolved_type` is `TD_KIND_SLICE`
- Build `struct_or_union_members_st` with auto-named fields (`_0`, `_1`, ...), each field type = `[]T` where T is the slice's element type
- Call `get_or_create_soa_type()` to create/find the SOA type
- `node->resolved_type` = SOA struct type

**IR generator**:
- Evaluate each arg → LLVM slice value
- Extract `.len` from each, compute `min_len` via comparison chain + `Select`
- Alloca SOA struct
- For each arg: GEP to field, create new slice with data=original.data, len=min_len, store
- Load and return struct

## `soa_unzip(soa_struct)`

**Semantics**: Takes a slice-backed SOA struct. Returns a tuple of N slices (one per field). Tuple support required.

**Grammar** (`odin_grammar.gdl`):
- Add `KwSoaUnzip = lexeme("soa_unzip" IdBoundary);` to `AllReservedWords`
- `SoaUnzipExpr = KwSoaUnzip LParen Expression RParen @AST_ACTION_SOA_UNZIP_EXPR;`
- Add to `UnaryExpression` alternatives

**AST**: `AST_NODE_SOA_UNZIP_EXPR`

**Semantic analyser** (requires tuple support):
- Evaluate arg
- Validate arg type is `TD_KIND_SOA` (slice-backed)
- For each backing field, create `[]T` slice type from field's element type
- Create tuple of these slice types via `get_or_create_tuple_type()`
- `node->resolved_type` = tuple type

**IR generator** (requires tuple destructuring):
- Evaluate arg (SOA struct value)
- Extract each field's data pointer + length via GEP
- Create a tuple LLVM struct with one slice per field
- Return the tuple struct

## Files to change

| File | Change |
|------|--------|
| `src/odin_grammar.gdl` | Keywords, rules, AllReservedWords, UnaryExpression |
| `src/odin_grammar_ast.h` | Two AST enum entries |
| `src/odin_grammar_ast_actions.c` | Two DEFINE_ACTION + REGISTER |
| `src/ast_node_name.c` | Two node name strings |
| `src/type_descriptors.h/c` | `TD_KIND_TUPLE` (for soa_unzip) |
| `src/semantic_analyser.c` | Two handler cases |
| `src/llvm_ir_generator.c` | Two handler cases + tuple destructuring |
| `tests/test_soa_zip.odin` | Tests |
| `notes/unsupported_features.md` | Mark as implemented |

## Prerequisites
- **Tuple type support** (`TD_KIND_TUPLE` + destructuring) — see `plan_tuple_support.md`
