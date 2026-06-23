# Context Built-in Implementation Plan

## Current State
- `KwContext` is a reserved lexeme but has **no grammar rule** making it a valid expression
- No calling convention support (no `proc "c"`, `proc "contextless"`)
- No calling convention field on `ProcMetadata`
- `get_or_create_proc_type` doesn't accept calling convention
- No implicit parameter injection anywhere
- No `Context` struct type defined
- No entry point detection (`main` is just another procedure constant)

## Dependency Graph

```
[Calling Conventions] ──────────────────────────────────┐
    (grammar + AST + sem + IR + type descriptors)        │
                                                         ▼
[Context Struct Type] ──────────────────────────► [Context Expression]
(built-in type registration)              (grammar + AST + sem + IR)
                                                         │
                                                         ▼
[Implicit Context Injection] ◄─────── [Caller Threading]
(callee: hidden param + memcpy)     (prepend arg in calls)
                                                         │
                                                         ▼
[Entry Point / Default Context]
(main detection + default Context)
```

---

## Phase 0: Calling Conventions

**Files**: `odin_grammar.gdl`, `odin_grammar_ast.h`, `odin_grammar_ast_actions.c`, `ast_node_name.c`, `type_descriptors.h`, `type_descriptors.c`, `semantic_analyser.c`

**Steps**:

1. **Grammar** (`odin_grammar.gdl`):
   - Add `CallingConvention = StringLiteral @AST_ACTION_CALLING_CONVENTION;`
   - Modify `ProcedureSignature = KwProc CallingConvention? Directive? ParameterList Returns? WhereClause?;`

2. **AST** (`odin_grammar_ast.h`): Add `AST_NODE_CALLING_CONVENTION`

3. **AST actions** (`odin_grammar_ast_actions.c`):
   - `DEFINE_ACTION(ast_action_calling_convention_action, AST_NODE_CALLING_CONVENTION)`
   - `REGISTER(AST_ACTION_CALLING_CONVENTION, ast_action_calling_convention_action)`

4. **Node names** (`ast_node_name.c`): Add `case AST_NODE_CALLING_CONVENTION: return "CallingConvention";`

5. **Type descriptors header** (`type_descriptors.h`):
   - Add `calling_convention_t` enum to `ProcMetadata`:
     ```c
     typedef enum {
         CALLING_CONV_ODIN,
         CALLING_CONV_CONTEXTLESS,
         CALLING_CONV_C,
         CALLING_CONV_STDCALL,
         CALLING_CONV_FASTCALL,
         CALLING_CONV_NONE,
     } calling_convention_t;
     ```
   - Add `calling_convention_t calling_convention;` field to `ProcMetadata`
   - Update `get_or_create_proc_type` declaration to accept `calling_convention_t cc`

6. **Type descriptors impl** (`type_descriptors.c`):
   - Update `get_or_create_proc_type` to accept and store `calling_convention_t cc`
   - Include CC in dedup comparison
   - Default to `CALLING_CONV_ODIN`

7. **Semantic analyser** (`semantic_analyser.c`):
   - In `sem_resolve_type_expr` (line ~397): detect `AST_NODE_STRING_LITERAL` child in ProcedureSignature, parse to enum
   - In `sem_analyse_procedure_literal` (line ~1259): same detection
   - Pass CC to `get_or_create_proc_type`

---

## Phase 1: Context Struct Type (Minimal — 4 fields)

**Files**: `type_descriptors.h`, `type_descriptors.c`, `semantic_analyser.c`

**Steps**:

1. **Header** (`type_descriptors.h`):
   - Add `context_type` field: `TypeDescriptor * context_type;` to `TypeDescriptors` (in the .c file struct)
   - Declare: `void register_builtin_context_types(TypeDescriptors * registry);`

2. **Impl** (`type_descriptors.c`):
   - `register_builtin_context_types()` creates:
     - `Allocator` struct: `{ procedure: rawptr, data: rawptr }` = 16 bytes
     - `Context` struct (minimal, 4 fields):
       ```c
       Context :: struct {
           allocator:    Allocator,   // 16 bytes
           temp_allocator: Allocator, // 16 bytes
           user_ptr:     rawptr,      // 8 bytes
           user_index:   int,         // 8 bytes (i64)
       }
       // Total: 48 bytes
       ```
   - Store Context TD on `registry->context_type`

3. **Semantic analyser** (`semantic_analyser.c`):
   - Call `register_builtin_context_types()` during semantic init (before pass 1)

---

## Phase 2: Context Expression

**Files**: `odin_grammar.gdl`, `odin_grammar_ast.h`, `odin_grammar_ast_actions.c`, `ast_node_name.c`, `semantic_analyser.c`, `llvm_ir_generator.c`

**Steps**:

1. **Grammar**: Add `ContextExpr = KwContext @AST_ACTION_CONTEXT_EXPR;` and add to `PrimaryExpression`
2. **AST**: Add `AST_NODE_CONTEXT_EXPR`
3. **Actions**: `DEFINE_ACTION` + `REGISTER`
4. **Node names**: Add case entry
5. **Semantic analyser**: `case AST_NODE_CONTEXT_EXPR:` → look up `"context"` in scope, return Context type
6. **IR generator**: `case AST_NODE_CONTEXT_EXPR:` → look up `"context"` symbol, return value

---

## Phase 3: Implicit Context Injection (Callee Side)

**Files**: `llvm_ir_generator.c`, `semantic_analyser.c`

**Steps**:

1. **`ir_gen_top_level_decl`** (`llvm_ir_generator.c`):
   - If CC == ODIN:
     - The func_type already includes `Context*` as first param (done in `get_or_create_proc_type`)
     - `LLVMGetParam(func, 0)` = context ptr
     - `alloca Context` (local struct)
     - `LLVMBuildMemCpy` from context ptr to alloca
     - Register `"context"` symbol with alloca, Context type, is_lvalue=true

2. **`ir_gen_register_params`**: If CC == ODIN, start `param_index` at 1 (skip context ptr)

3. **`ir_gen_nested_procedure_decl`**: Same changes as `ir_gen_top_level_decl`

4. **`sem_analyse_procedure_literal`**: Register `"context"` symbol with Context type (value=NULL) after scope push

---

## Phase 4: Caller Context Threading

**Files**: `llvm_ir_generator.c`

**Steps**:

1. **In `AST_NODE_POSTFIX_CALL` handler**:
   - If callee CC == ODIN:
     - Look up `"context"` in current scope
     - Prepend the context alloca ptr as first call argument
     - If no context found (top-level), use a global zero-initialized Context

2. **`get_or_create_proc_type` adjustment**: For ODIN CC, prepend `Context*` (pointer to Context) to LLVM param list in `func_type`

---

## Phase 5: Entry Point Wrapper

**Files**: `llvm_ir_generator.c` (or `main.c`)

**Steps**:

1. **Detect `main`**: In `ir_generate`, check if any top-level proc is named `"main"`
2. **Generate wrapper**: Create C `main` function:
   - `alloca Context`, LLVMConstNull (zero-initialized)
   - Call user's `main` with the context
   - Return the int result
3. **Support both**: `main :: proc() -> int` and `main :: proc()`

---

## Phase 6: Semantic Analyser Changes

**Files**: `semantic_analyser.c`

**Steps**:

1. Register `"context"` symbol in ALL procedures (not just ODIN)
2. For non-ODIN procs: `context` is available but not threaded (zero-initialized local)

---

## Summary

| Phase | Files Changed | Est. Lines |
|-------|---------------|------------|
| 0. Calling Conventions | 7 files | ~200 |
| 1. Context Struct Type | 3 files | ~100 |
| 2. Context Expression | 6 files | ~40 |
| 3. Implicit Injection | 2 files | ~150 |
| 4. Caller Threading | 1 file | ~60 |
| 5. Entry Point | 2 files | ~60 |
| 6. Sem Changes | 1 file | ~30 |
| **Total** | **~10 files** | **~640** |

Key design decisions:
- **Full calling convention enum** with ODIN, CONTEXTLESS, C, STDCALL, FASTCALL, NONE
- **Memcpy approach**: Copy Context struct from hidden param to local alloca (simple, correct)
- **Minimal Context**: 4 fields (allocator, temp_allocator, user_ptr, user_index) = 48 bytes
- **Context always in scope**: Available in all procedures, only threaded for ODIN CC
