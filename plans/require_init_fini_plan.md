# @require import + @(init)/@(fini) Implementation Plan

## Current State

- `@require import "pkg"`: Not in grammar — causes parse error (`@` followed by `require` doesn't match `At LParen`)
- `@(init)` / `@(fini)`: Not parsed — `sem_analyse_attributes` only handles `link_name`, `require_results`, `private`, `builtin`
- Import DCE: Not implemented — ALL imports are unconditionally parsed, analysed, and codegen'd regardless of use
- Init/fini calls: Not implemented — entry point only calls `__odin_main`

## Phase 0: Grammar + Attribute Support (~100 lines)

### 0a: @require import grammar
- Add `KwRequire` lexeme to `odin_grammar.gdl`
- Add `ImportRequire = At KwRequire ImportDecl @AST_ACTION_IMPORT_REQUIRE` rule
- Add `ImportRequire` to `ExternalDeclarations` alternatives
- Add `AST_NODE_IMPORT_REQUIRE` to `odin_grammar_ast.h` enum
- Add action function in `odin_grammar_ast_actions.c`
- Add node name in `ast_node_name.c`
- In `semantic_analyser.c`, handle `AST_NODE_IMPORT_REQUIRE` by unwrapping child import node

### 0b: @(init)/@(fini) attribute parsing
- Add `is_init`, `is_fini` fields to `ProcDeclAttributes` in `type_descriptors.h`
- Handle `"init"` → `attrs->is_init = true` and `"fini"` → `attrs->is_fini = true` in `sem_analyse_attributes`

## Phase 1: Import Usage Tracking (~200 lines)

Goal: After semantic analysis, determine which imports actually have symbols referenced.

### Detect symbol references per import
- After `sem_pass2_analyse_bodies_ast`, walk the importing file's AST
- For each `AST_NODE_POSTFIX_EXPRESSION` with a package-qualified first operand, check which import it resolves to
- For `import using` packages, track which copied symbols are actually referenced
- Add `bool is_used` field to `ImportedPackage`
- Unwrap `AST_NODE_IMPORT_REQUIRE` → always mark as `is_used = true`

### Alternative simpler approach
- Just check `PackageClause` → for each import, scan all identifiers in the file and see if any match `pkg_name.identifier` pattern
- Less precise but much simpler

## Phase 2: Skip Codegen for Unused Imports (~150 lines)

- In `ir_generate()`, skip `ir_gen_process_ast(pkg->ast)` when `!pkg->is_used`
- Skip `llvm.dependent.libraries` metadata for unused imports' foreign libs
- Skip init/fini proc collection for unused imports

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
