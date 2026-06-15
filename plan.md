# Odin Compiler (`odinc`) — Project Plan

## Overview
Build an Odin programming language compiler in C, using:
- **easy_pc** (parser combinator library) for parsing via GDL grammar
- **LLVM 20** for IR code generation and native compilation

Modeled on the C compiler at `/home/chris/projects/c_compiler`.

---

## Phase 1: Parsing & AST Building  ✅ (DONE)

- `odin_grammar.gdl` — Full GDL grammar covering all Odin syntax
- `odin_grammar_ast.h` — Union-based AST node type enum and node struct
- `odin_grammar_ast_actions.c/h` — Semantic action callbacks, registry init
- `main.c` — CLI entry: parse file, build AST, print CPT/AST
- `CMakeLists.txt` — easy_pc + LLVM integration

---

## Phase 2: Two-Pass Semantic Analysis + LLVM IR Codegen

### Architecture

```
Source (.odin)
    → Parser (GDL) → CPT
    → AST Builder (epc_ast_build) → odin_grammar_node_t* (annotatable)
    → Semantic Analyser (Pass 1)
        • Sub-pass 1a: Register all top-level declarations (sig/type pre-scan)
        • Sub-pass 1b: Walk procedure bodies, resolve names, type-check, annotate
    → LLVM IR Generator (Pass 2)
        • Walk annotated AST, read resolved_type/resolved_symbol, emit LLVM IR
    → LLVMModuleRef → .ll / .o / .s
```

### Files

#### Step 1 — Reusable Infrastructure (copy/adapt from C compiler)
| File | Source | Notes |
|---|---|---|
| `generic_hash_table.h/c` | C compiler | Copy verbatim |
| `debug.h/c` | C compiler | Copy, update include guard prefix |
| `source_location.h/c` | C compiler | Copy verbatim |
| `ast_node_name.h/c` | C compiler | Rewrite `get_node_type_name()` for Odin node types |

#### Step 2 — Type System Foundation
| File | Description |
|---|---|
| `type_kinds.h` | `TYPE_KIND_BASIC`, `POINTER`, `ARRAY`, `SLICE`, `DYNAMIC_ARRAY`, `STRUCT`, `UNION`, `ENUM`, `BIT_FIELD`, `BIT_SET`, `MAP`, `PROC`, `DISTINCT`, `SOA` |
| `type_descriptors.h/c` | `TypeDescriptor` struct with kind + union of type-specific data; registry with dedup; basic LLVM type mapping |
| `typed_value.h/c` | `TypedValue`: LLVMValueRef + TypeDescriptor* + is_lvalue |
| `symbols.h` | `symbol_t`: name + TypedValue + SymbolKind enum |
| `struct_members.h` | `struct_field_t`: name + type + offset |

#### Step 3 — Scope / Symbol Table System
| File | Description |
|---|---|
| `scope.h/c` | Hierarchical scope with parent chain, symbol lookups, type lookups |
| `scope_lists.h/c` | Hash-table-backed per-scope storage (symbols, type lists) |
| `generator_lists.h/c` | Codegen bridge: scope push/pop, symbol add/lookup |

#### Step 4 — Semantic Analyser (Pass 1)
| File | Description |
|---|---|
| `sem_error.h/c` | Error collection for semantic analysis |
| `semantic_analyser.h/c` | Two-pass AST walker: (a) register top-level decls, (b) type-check bodies |

AST node annotation fields (added to `odin_grammar_ast.h`):
```c
TypeDescriptor * resolved_type;   // For expressions and declarations
Symbol * resolved_symbol;          // For identifier references
```

#### Step 5 — LLVM IR Generator (Pass 2)
| File | Description |
|---|---|
| `ir_utils.h/c` | `generate_anon_name()`, `get_type_alignment()`, `aligned_store/load()` |
| `ir_gen_error.h/c` | Error/warning collection during codegen with source location |
| `llvm_ir_generator.h/c` | Generator context + recursive AST walker emitting LLVM IR |

#### Step 6 — Pipeline Integration
| File | Changes |
|---|---|
| `main.c` | CLI: `odinc build`, `odinc check`, `odinc version`; wire phases together |
| `src/CMakeLists.txt` | Add all new source files |

---

## Implementation Order

Each step is buildable and testable independently.

### Step 1: Infrastructure
Copy `generic_hash_table`, `debug`, `source_location`, write `ast_node_name` for Odin. Update `CMakeLists.txt`.

### Step 2: Type System
`type_kinds.h` → `type_descriptors.h/c` → `typed_value.h/c` → `symbols.h` → `struct_members.h`

### Step 3: Scope System
`scope.h/c` → `scope_lists.h/c` → `generator_lists.h/c`

### Step 4: Semantic Analyser
`sem_error.h/c` → `semantic_analyser.h/c`
- 4a: registration pass (top-level procs, types, globals)
- 4b: body analysis (type-check, annotate)

### Step 5: LLVM IR Generator
`ir_utils.h/c` → `ir_gen_error.h/c` → `llvm_ir_generator.h/c`
- Handle: Program, PackageClause, ConstantDecl (proc), CompoundStatement, ReturnStatement, ExpressionStatement, identifiers, literals, binary ops, assign, variable decl

### Step 6: Pipeline + CLI
Update `main.c` with odin-style CLI, wire all phases, output to file.

---

## Testing

Trivial first program (`test_return.odin`):
```odin
package main

main :: proc() -> int {
    return 42;
}
```

Expected:
```
$ odinc build test_return.odin -out:test_return
$ ./test_return
$ echo $?
42
```

---

## CLI Design

```
odinc build <file>                # parse, check, codegen, link → executable
odinc build <file> -out:<path>    # output to specific path
odinc build <file> -o:llvm-ir     # emit .ll IR text
odinc build <file> -o:object      # emit .o object file
odinc build <file> -o:assembly    # emit .s assembly
odinc check <file>                # parse + type-check only
odinc version                     # print version
odinc help                        # print usage
```

Output type flags (`-o:`):
- `llvm-ir` (default for `build` if no output type specified and linker not available)
- `object` (`.o`)
- `assembly` (`.s`)
- `none` (check only, implied by `check` subcommand)

---

## LLVM Type Lowerings (Odin → LLVM)

| Odin Type | LLVM IR |
|---|---|
| `int` | `i64` (on 64-bit) |
| `i8`/`i16`/`i32`/`i64` | `i8`/`i16`/`i32`/`i64` |
| `u8`/`u16`/`u32`/`u64` | `i8`/`i16`/`i32`/`i64` |
| `f32`/`f64` | `float`/`double` |
| `bool` | `i1` |
| `string` | `{ i8*, i64 }` |
| `rune` | `i32` |
| `^T` | `T*` |
| `[N]T` | `[N x T]` |
| `[]T` | `{ T*, i64 }` |
| `[dynamic]T` | `{ T*, i64, i64, RuntimeAllocator }` |
| `any` | `{ i8*, i64 }` |
| union | `{ [max_size x i8], i32 }` |

---

## Deferred to Later Phases

- Multi-file package compilation (directory scan)
- Implicit context parameter on every procedure
- `any` type / RTTI runtime operations
- Tagged unions / `switch type`
- `using` (struct embedding + name shadowing) — see **Tasks: Struct Member Access** below

## Tasks: Block Scoping

Current status: Procedure bodies have a scope, but `if`/`for`/`switch` bodies and standalone `{ }` blocks do NOT push a scope. All variables in a procedure are flat.

### Semantic Analyser Changes (`semantic_analyser.c`)

Each location that processes a block body needs `generator_push_scope` / `generator_pop_scope`:

| Location | Line | Description |
|---|---|---|
| `AST_NODE_COMPOUND_STATEMENT` (pass2 dispatch) | 731 | Standalone `{ }` blocks |
| `AST_NODE_IF_STATEMENT` (then/else bodies) | 811-813 | Each body compound statement |
| `AST_NODE_FOR_STATEMENT` (loop body) | 832 | For-loop body |
| `AST_NODE_SWITCH_CASE` (case bodies) | 843-870 | Each case clause body |
| `AST_NODE_SWITCH_DEFAULT` (default body) | 871-878 | Default clause body |

### IR Generator Changes (`llvm_ir_generator.c`)

Same locations in the IR generator — wrap compound statement calls with push/pop:

| Location | Function | Description |
|---|---|---|
| `ir_gen_compound_statement` callers | `ir_gen_if`, `_for`, `_switch` | Bodies need scope |

### Test

`test/test_scope.odin` — verify variable isolation across control flow blocks and shadowing.

## Tasks: Struct Member Access (`using` support)

### Background

In Odin, `using` on a struct field "promotes" the inner struct's fields into the parent scope, so `outer.field` works without naming the intermediate struct. Example:

```odin
Inner :: struct { y: int }
Outer :: struct { using inner: Inner; x: int }
// outer.y works — y is promoted from inner
```

Current state: None of this works. The grammar has `KwUsing?` in `StructField` but the semantic analyser ignores it. `type_descriptor_find_struct_field_index` only scans direct fields — it does not recurse.

### Changes Needed

1. **`struct_members.h`**: Add `bool is_using` flag to `struct_field_t`
2. **`semantic_analyser.c`** (struct field registration): Detect `KwUsing` presence (via `AST_NODE_USING_DECL` or checking the struct field's children for a `using`-related node) and set `is_using` on the field
3. **`type_descriptors.c`** (`type_descriptor_find_struct_field_index`): When a direct field lookup fails, recurse into `using`-marked struct fields. Handle name conflicts (direct fields shadow promoted ones; among siblings, first match wins — or last-declared wins, per Odin spec)
4. **`llvm_ir_generator.c`** (GEP for promoted fields): When member access resolves to a promoted field, build the full GEP index chain (e.g., to access `outer.y`, GEP through `inner` then `y`)
5. **Test**: `test/test_using.odin` — verify promoted field access and shadowing
- Polymorphic procedures / monomorphisation
- `when` compile-time branch pruning
- `or_else` / `or_return` / `defer` codegen
- Built-in procedures: `make`, `new`, `delete`, `len`, `cap`, `append`
- `transmute` / `cast`
- `bit_field`, `bit_set`, `map` type codegen
- `foreign` blocks / external linking
- `soa` (structure-of-arrays) layout
