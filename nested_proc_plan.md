# Nested Procedures as Values — Implementation Plan

## Background

In Odin, procedures can be declared as values inside other procedures:

```odin
main :: proc() -> int {
    add :: proc(a: int, b: int) -> int {
        return a + b
    }
    return add(1, 2)
}
```

The inner `add :: proc(...) { ... }` is a `ConstantDecl` with a `ProcedureLiteral` value, nested inside a `CompoundStatement` within another procedure.

## Current State

Currently, `ConstantDecl` handling only works at **top level** (inside `ExternalDeclarations`). Inside procedure bodies:

1. **Semantic analyser** (`sem_pass2_node`): does NOT handle `AST_NODE_CONSTANT_DECL` inside procedure bodies. If a constant decl (like a nested proc) appears inside a compound statement, it's silently skipped.
2. **IR generator** (`ir_gen_node`): handles `AST_NODE_CONSTANT_DECL`, but `ir_gen_top_level_decl` assumes the function is already created and appended to the module — this works for top-level because the outer loop in `ir_generate` creates and registers functions at module scope. For nested procs, each needs its own `LLVMAddFunction`.

## Changes Required

### 1. Semantic Analyser (`semantic_analyser.c`)

In `sem_pass2_node`, add a case for `AST_NODE_CONSTANT_DECL` inside procedure bodies. This should:
- Register the name in the current scope (like `sem_register_top_level_declaration` does)
- If the value is a procedure literal, perform semantic analysis on it (like `sem_analyse_procedure_literal` does)
- Otherwise, evaluate the value expression

### 2. IR Generator (`llvm_ir_generator.c`)

Add a `ir_gen_nested_procedure_decl` function (or extend `ir_gen_node` for `AST_NODE_CONSTANT_DECL` inside bodies) that:
- Creates a new LLVM function (`LLVMAddFunction`) using the proc type
- Builds the function body (same as top-level proc codegen)
- Registers the function value in the current scope as a symbol (so calls can reference it)

The existing `ir_gen_top_level_decl` code can be factored out and reused — the key difference is where the symbol is registered (current scope vs top-level scope) and that the function is given its real name (not an anonymous name like `ir_gen_procedure_literal`).

## Test

`test/test_nested_proc.odin` — nested procedure with parameter passing, return value, and verification.

## Files to Modify
- `src/semantic_analyser.c` — add `AST_NODE_CONSTANT_DECL` handling in `sem_pass2_node`
- `src/llvm_ir_generator.c` — add nested constant decl (proc) codegen in `ir_gen_node`
- `test/test_nested_proc.odin` — new test

## Verification
- Build: `cmake --build build`
- Test: `./tests/run_tests.sh`
- All existing tests must still pass
