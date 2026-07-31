# @require import + @(init)/@(fini) Implementation Plan

## Current State

- `@require import "pkg"`: Implemented (Phase 0a)
- `@(init)` / `@(fini)`: Parsed (Phase 0b), but not yet collected or called
- Import DCE: Implemented (Phase 1)
- Init/fini calls: Not implemented — entry point only calls `__odin_main`

## Phase 0: Grammar + Attribute Support (~100 lines)

### 0a: @require import grammar ✅ DONE
- Added `KwRequire` lexeme to `odin_grammar.gdl`
- Added `ImportRequire = At KwRequire ImportDecl @AST_ACTION_IMPORT_REQUIRE` rule
- Added `ImportRequire` to `ExternalDeclarations` alternatives
- Added `AST_NODE_IMPORT_REQUIRE` to `odin_grammar_ast.h` enum
- Added action function in `odin_grammar_ast_actions.c`
- Added node name in `ast_node_name.c`
- In `semantic_analyser.c`, handle `AST_NODE_IMPORT_REQUIRE` by unwrapping child import node

### 0b: @(init)/@(fini) attribute parsing ✅ DONE
- Added `is_init`, `is_fini` fields to `ProcDeclAttributes` in `type_descriptors.h`
- Handle `"init"` → `attrs->is_init = true` and `"fini"` → `attrs->is_fini = true` in `sem_analyse_attributes`

## Phase 1: Import Usage Tracking ✅ DONE

Goal: After semantic analysis, determine which imports actually have symbols referenced.

### Implementation
- Added `bool is_used` and `bool is_direct_import` fields to `ImportedPackage`
- Added `int import_reg_depth` to `SemContext` to track recursion depth during import registration
- Non-using imports: marked used when `sem_evaluate_postfix_expr` detects package-qualified reference (`pkg.symbol`)
- Using imports: walked AST after pass 2, checking if resolved identifiers match symbols in the using import's scope
- Transitive imports: marked used by default (conservative approach - LLVM DCE cleans up unused code)

### Essential Packages Heuristic ⚠️
The following packages are always processed even if unused, as they provide runtime support:
- `os`: Provides syscall wrappers (`sys_open`, `sys_read`, `sys_write`, `sys_close`) needed by `core:io`
- `io`: Provides I/O primitives that may be referenced transitively
- `runtime`: Provides `os_exit`, `print_string`, `int_to_string` intrinsics used by all programs
- `mem`: Provides allocator infrastructure (`mem_alloc`, `mem_free`) needed by runtime

This is a conservative approach - a proper implementation would track actual dependencies.

## Phase 2: Skip Codegen for Unused Imports ✅ DONE

- In `ir_generate()`, skip `ir_gen_process_ast(pkg->ast)` when `!pkg->is_used`
- Skip `import_using_copy_symbol` for unused packages
- Essential packages are always processed (see Phase 1 heuristic)

## Phase 3: Collect and Call @(init)/@(fini) (~200 lines)

### Data structures
- Add `LLVMValueRef init_procs[MAX_INIT_FINI]; int init_proc_count;` (and fini variants) to `IrGenContext`

### Collection during codegen
- In `ir_gen_top_level_decl`, after emitting a procedure body, check `attrs->is_init`/`attrs->is_fini`
- If set, append the function value to the appropriate array

### Entry point integration
- After context setup in `ir_generate`, emit calls to all `init_procs[]` before `__odin_main`
- After `__odin_main` returns (or via `atexit`), call all `fini_procs[]`
- Init/fini signature is `proc()` (void args, void return)

### atexit for os.exit() support
- Register `atexit` handler that calls all fini procs, so `os.exit()` properly runs them
- LLVM has `LLVMAddFunction(ctx->module, "atexit", ...)` — need to call it per-fini-proc during init

## Phase 4: Tests (~150 lines)

- `tests/test_init_fini.odin`: Verify init runs before main, fini runs after
- `tests/test_require_import.odin`: Parse test for `@require import "pkg"`
- `tests/test_import_unused.odin`: Verify unused import is skipped (no codegen)
- `expected_to_fail/test_require_parse_without_at.odin`: Ensure bare `require import "pkg"` fails

## Timeline

| Phase | Est. lines | Est. time |
|-------|-----------|-----------|
| 0a: @require grammar + AST | ~50 | 30 min |
| 0b: @(init)/@(fini) parsing | ~20 | 10 min |
| 0c: Semantic handler | ~30 | 15 min |
| 1: Usage tracking | ~200 | 2 hr |
| 2: Skip codegen | ~150 | 1 hr |
| 3: Init/fini collection + calls | ~200 | 2 hr |
| 4: Tests | ~150 | 1 hr |
| **Total** | **~800** | **~7 hr** |
