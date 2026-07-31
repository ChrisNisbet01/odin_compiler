# @require import + @(init)/@(fini) Implementation Plan

## Current State

- `@require import "pkg"`: Implemented (Phase 0a)
- `@(init)` / `@(fini)`: Parsed and called (Phases 0b, 3)
- Import DCE: Implemented (Phase 1)
- Init/fini calls: Implemented (Phase 3)

## Phase 0: Grammar + Attribute Support

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
- Foreign libraries from skipped imports are not emitted (they're only added during codegen)
- Essential packages (`os`, `io`, `runtime`, `mem`) are always processed

## Phase 3: Collect and Call @(init)/@(fini) ✅ DONE

### Data structures added to `IrGenContext`
- `LLVMValueRef init_procs[128]; int init_proc_count;`
- `LLVMValueRef fini_procs[128]; int fini_proc_count;`

### Collection during codegen
- In `ir_gen_top_level_decl`, after emitting a procedure body, check `attrs->is_init`/`attrs->is_fini`
- If set, append the function value to the appropriate array

### Entry point integration
- Call all `init_procs[]` before `__odin_main` (pass context pointer as argument)
- Register all `fini_procs[]` with `atexit()` so they run when `os.exit()` is called or process exits normally
- Init/fini signature is `proc()` (void args, void return)

## Phase 4: Tests (Remaining Work)

- `tests/test_init_fini.odin`: Verify init runs before main, fini runs after
- `tests/test_require_import.odin`: Parse test for `@require import "pkg"`
- `tests/test_import_unused.odin`: Verify unused import is skipped (no codegen)
- `expected_to_fail/test_require_parse_without_at.odin`: Ensure bare `require import "pkg"` fails

## Summary

All core functionality is implemented and all 215 existing tests pass. Remaining work is adding dedicated tests for the new features.