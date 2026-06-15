# Implementation Roadmap — Odin Compiler

Difficulty-sorted list of unimplemented/partially-implemented features.
Each tier progresses from self-contained items to those requiring deeper infrastructure.

---

## Tier 1 — Self-contained, minimal new infrastructure

### 1. `cast` / `transmute` expressions
**Why easy:** Grammar lexemes exist (`KwCast`, `KwTransmute`, `KwAutoCast`) but are
not wired to any grammar rule. Need to add grammar productions, AST actions,
semantic analysis (type-checking rules), and IR codegen (`LLVMBuildBitCast` /
`LLVMBuildPointerCast` etc.). No new type system infrastructure needed.

**Files:** `src/odin_grammar.gdl`, `src/odin_grammar_ast.h` (new AST nodes),
`src/odin_grammar_ast_actions.c/h`, `src/semantic_analyser.c`,
`src/llvm_ir_generator.c`

**Tests:** `test/test_cast.odin`, `test/test_transmute.odin`

### 2. Postfix deref (`^` operator)
**Why easy:** Grammar rule `PostfixOpDeref` exists, `AST_NODE_POSTFIX_DEREF` exists,
AST action exists. No semantic or IR handling. Just needs type check (source must
be a pointer) + `LLVMBuildLoad` in the lvalue path, `LLVMBuildLoad` in the rvalue path.

**Files:** `src/semantic_analyser.c`, `src/llvm_ir_generator.c`

**Tests:** `test/test_deref.odin`

### 3. Slice subscript & slicing operations
**Why easy:** Grammar rules `PostfixOpSubscript` (shared with arrays) and
`PostfixOpSlice`/`PostfixOpSliceLt` exist, AST nodes exist, actions exist.
Subscript currently only handles arrays — needs slice bounds-check. Slice
operations need to construct new slice headers `{ptr+offset, len}`.

**Files:** `src/semantic_analyser.c`, `src/llvm_ir_generator.c`

**Tests:** `test/test_slice_ops.odin`

### 4. `fallthrough` statement codegen
**Why easy:** Grammar ✓, AST ✓, semantic stub (type-checked in switch body).
IR generator needs to emit a branch to the next case block.

**Files:** `src/llvm_ir_generator.c`

**Tests:** Extend `test_switch.odin` or add `test_fallthrough.odin`

---

## Tier 2 — Scoped state management

### 5. `defer` statement codegen
**Why medium:** Grammar ✓, AST ✓, semantic stub. Need scope-based defer
stack in the IR generator. Each scope tracks deferred actions; they execute
in reverse order on scope exit (block end) and on function exit (return / end).
Requires careful IR builder position save/restore. Well-defined in Odin spec.

**Files:** `src/llvm_ir_generator.h` (defer stack in context),
`src/llvm_ir_generator.c` (emit deferred actions on scope pop, return, function end)

**Tests:** `test/test_defer.odin`

---

## Tier 3 — Compile-time evaluation

### 6. `when` compile-time branch
**Why medium:** Grammar ✓, AST ✓ (both `WHEN_STATEMENT` and `WHEN_DECL`).
No semantic or IR handling. Requires:
- Constant expression evaluation engine (folding for integer/bool/string/unary/binary ops)
- In semantic analyser: evaluate `when` condition, validate bool, only analyse matching branch
- In IR generator: only emit matching branch

The constant evaluator is the main piece of work, but well-defined and useful
for all future constant-expression features.

**Files:** Add `constant_evaluator.h/c` (new), `src/semantic_analyser.c`,
`src/llvm_ir_generator.c`, `src/CMakeLists.txt`

**Tests:** `test/test_when.odin`

---

## Tier 4 — Type system extensions

### 7. `or_else` / `or_return` statements
**Why medium-hard:** Grammar ✓, AST ✓ (`OR_ELSE`, `OR_RETURN`), semantic stubs.
Requires:
- "Optional ok" type pattern: `(value, bool)` tuple or nil-able types
- Type-splitting in semantic analysis (extract value type from ok discriminator)
- IR generation with conditional branches + phi nodes

**Files:** `src/type_descriptors.h/c` (optional type helpers),
`src/semantic_analyser.c`, `src/llvm_ir_generator.c`

**Tests:** `test/test_or_else.odin`, `test/test_or_return.odin`

### 8. `len` / `cap` built-in procedures
**Why medium-hard:** No lexemes, no grammar rules, no AST nodes yet.
Requires:
- Add `len`, `cap` to grammar as builtin call syntax or identifier-based detection
- Register as built-in symbols in the builtin scope
- Semantic checks: validate operand type (string → str.len, array → const, slice → slc.len, etc.)
- Codegen: load struct field or return constant
- Infrastructure needed: "builtin procedure" detection mechanism

**Files:** `src/odin_grammar.gdl`, `src/odin_grammar_ast.h`,
`src/odin_grammar_ast_actions.c/h`, `src/semantic_analyser.c`,
`src/llvm_ir_generator.c`

**Tests:** `test/test_len_cap.odin`

### 9. `make` / `new` / `delete` / `append` builtins
**Why hard:** Grammar lexemes exist but unused. Need full builtin procedure
infrastructure + runtime memory allocation support.

**Files:** Many. Separate plan needed.

---

## Tier 5 — Runtime infrastructure

### 10. `any` type / RTTI
**Why very hard:** Current `any` type uses wrong LLVM type (`string_llvm`).
Need:
- Correct `any` type layout: `{rawptr, typeid}` (16 bytes on 64-bit)
- `typeid` system: 64-bit hash of canonical type string (SipHash)
- Global TypeInfo array with one entry per unique type
- Implicit conversion from any value to `any` (store ptr + typeid)
- Type assertion `.()` codegen (`compare typeid, branch on mismatch`)
- Semantic analysis for type assertions

**Files:** `src/type_descriptors.h/c`, `src/semantic_analyser.c`,
`src/llvm_ir_generator.c`, new hash/util files

**Tests:** `test/test_any.odin`

### 11. Tagged unions / `switch type`
**Why very hard:** Depends on RTTI/typeid system.
- Union variants need a tag field (integer discriminator)
- `switch type val { ... }` needs to compare runtime type against case types
- Needs typeid infrastructure from `any`/RTTI item

**Tests:** `test/test_union.odin`

### 12. `bit_field` / `bit_set` type codegen
**Why medium:** Grammar ✓, AST ✓, type `TD_KIND` enum ✓.
Need LLVM lowering for bit-packed fields and bit-set operations.
Moderate complexity, self-contained.

**Tests:** `test/test_bit_field.odin`, `test/test_bit_set.odin`

### 13. `map` type codegen
**Why very hard:** Hash map with dynamic allocation, resizing, key/value access.
Grammar ✓, AST ✓, type `TD_KIND_MAP` ✓. Need full runtime hash table.

**Tests:** `test/test_map.odin`

---

## Tier 6 — External/system integration

### 14. `foreign` blocks / external linking
**Why medium-hard:** Grammar ✓, AST ✓. Need:
- Track foreign library names per declaration
- Emit external symbol references in LLVM IR
- Linker integration for `-l` flags

**Tests:** `test/test_foreign.odin`

### 15. `soa` (structure-of-arrays) layout
**Why very hard:** Grammar ✓, AST ✓, type `TD_KIND_SOA` ✓.
Need layout transformation that splits struct fields into parallel arrays.
Changes GEP indices for member access. Complex IR transformation.

**Tests:** `test/test_soa.odin`

---

## Tier 7 — Major language features

### 16. Polymorphic procedures / monomorphisation
**Why very hard:** Requires template instantiation system.
- Grammar has `PolyIdent` (`$T`) but no semantic handling
- Need to detect polymorphic parameters, create template instances
- Specialize for each unique type parameter combination

**Tests:** `test/test_poly.odin`

---

## Legend

| Difficulty | Icon | Typical scope |
|---|---|---|
| Easy | ✅ | 1-3 files, <100 LOC, self-contained |
| Medium | 🔶 | 2-5 files, 100-500 LOC, some new infra |
| Hard | 🔴 | 3-8 files, 500-2000 LOC, significant new infra |
| Very Hard | ❌ | 5+ files, 2000+ LOC, major subsystem |
