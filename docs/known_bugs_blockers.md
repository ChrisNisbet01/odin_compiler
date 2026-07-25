# Known Bugs & Blockers Tracking

## Status Legend
- ✅ Fixed
- 🔧 In Progress
- ❌ Open
- ℹ️ By Design (not a bug)

---

## Parser Bugs

### 1. Empty struct literal `Type{}` fails to parse ✅
- **Symptom**: `v := MyStruct{}` gives "End of input not found"
- **Root cause**: `StructLitExpr` rule requires `StructLitFields` (no `?`), and `delimited_flex` requires ≥1 item
- **Fix**: Add `?` to `StructLitFields` in `StructLitExpr` rule
- **Grammar location**: `src/odin_grammar.gdl:378`
- **Discovered**: 2026-07-24
- **Fixed**: 2026-07-24
- **Test**: `tests/test_struct_literal_empty.odin`

### 2. `return Type{...}` struct literal fails to parse ✅
- **Symptom**: `return MyStruct{x = 0}` gives "End of input not found"
- **Root cause**: `ReturnStatement` uses `AssignExpression` instead of `ExpressionOrStructLit`
- **Fix**: Created `ReturnExpr` rule (`StructLitExpr | ArrayLitExpr | AssignExpression`) and changed `ReturnStatement` to use it
- **Grammar location**: `src/odin_grammar.gdl:524-525`
- **Discovered**: 2026-07-24
- **Fixed**: 2026-07-24
- **Test**: `tests/test_return_struct_literal.odin`

### 3. `enum i32` backing type fails to parse ✅
- **Symptom**: `MyEnum :: enum i32 { A, B }` gives "Expected: not Allreservedwords / Found: 'i32'"
- **Root cause**: `EnumType` only accepts `(Directive IntegerLiteral?)?` for backing type (requires `#` prefix), not bare `TypePrefix`
- **Fix**: Replaced `(Directive IntegerLiteral?)?` with `BasicType?` in `EnumType` rule
- **Grammar location**: `src/odin_grammar.gdl:315`
- **Discovered**: 2026-07-24
- **Fixed**: 2026-07-24
- **Test**: `tests/test_enum_backing_type.odin`

### 4. `enum i64` / `enum u32` backing types ✅
- Same root cause as #3 — fixed together

---

## Missing Features (Blockers for strings.Builder)

### 5. `append()` builtin ✅
- **Symptom**: `append(&arr, elem)` — no grammar rule, AST node, semantic handler, or IR codegen
- **Fix**: Implemented `AppendExpr` grammar rule, `AST_NODE_APPEND_EXPR`, semantic analysis, and IR generation with malloc/realloc/memcpy growth
- **Date implemented**: 2026-07-24
- **Segfault fixed**: 2026-07-25 — two bugs:
  1. Missing `LLVMBuildLoad2` before store (arr_val was alloca pointer, not struct value)
  2. PHI node incorrect incoming block (`grow_bb` → `free_done_bb` — the store block's actual predecessor)
- **Test**: `tests/test_append.odin` (single, multiple, from-empty)

### 6. Runtime intrinsics for strings.Builder functions ❌
- **Needed for**: `write_byte`, `write_bytes`, `write_string`, `to_string`, `builder_make_none`
- **Current approach**: Bodyless `---` procs with `@(builtin)` attribute + IR intrinsic body generation
- **Status**: Stub declarations exist in `stubs/core/strings/strings.odin`, need intrinsic body handlers OR pure Odin implementation
- **Discovered**: 2026-07-24

---

## Not Bugs (Confirmed Working)

### ℹ️ `_ = x` discard for struct/alias types
- **Status**: Works correctly at all layers (parser, semantic, IR)
- **Confirmed**: All types work — basic, struct, alias, distinct, string, array, bool, float
- **No fix needed**
