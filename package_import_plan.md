# Package Import Support Plan

## Current State Summary

| Feature | Status | Location |
|---------|--------|----------|
| `package` clause parsing | ✅ Implemented | `semantic_analyser.c`: package name extracted, used for import resolution |
| `import "path"` parsing | ✅ Implemented | Recursive pass1/pass2, cycle detection |
| `import name "path"` parsing (named) | ✅ Implemented | Alias overrides `package_name` in `ImportedPackage` |
| `import using "path"` | ✅ Implemented | Symbols copied into current scope via `import_using_copy_symbol` |
| `import` semantic analysis | ✅ Implemented | Pass1 registers symbols, pass2 analyses bodies, cross-package resolution via `find_imported_package_by_name` |
| `import` IR codegen | ✅ Implemented | Import processing loop in `ir_generate()`, package-qualified member access handles `resolved_symbol` |
| Cross-package function calls | ✅ Implemented | `pkg.func()` and `import using` + `func()` both work |
| Cycle detection | ✅ Implemented | `import_stack` tracks in-progress imports |
| `ODIN_ROOT` / search paths | ✅ Implemented | `resolve_odin_root()` with `realpath`, `resolve_import_path()` for search algorithm |
| `foreign import name "lib"` | ✅ Implemented | IR: collects library names → LLVM metadata |
| `foreign name { decls }` | ✅ Implemented | Registers decls, emits code |
| Multi-file compilation | ✅ Implemented | Single-module approach — imports generate IR into same module |
| Linker invocation | ❌ Not implemented | Handled by test script `clang` call |
| Object file emission | ❌ Exists but unused | `emit_to_file()` in `llvm_ir_generator.c` |
| Symbol visibility / export | ⚠️ Basic | `main` gets private linkage for wrapper; imported symbols use default linkage |
| Standard library stubs | ❌ Not implemented | No `builtin`, `core`, or `vendor` packages yet |
| `import using` constant refs | ✅ Implemented | Re-copy loop refreshes LLVM values after import codegen |

## Architecture Overview

The official Odin compiler uses `ODIN_ROOT` to locate the standard library packages:
- Default: directory containing the compiler executable (`argv[0]`-relative, `../..` from the compiler binary's location)
- Can be overridden via the `ODIN_ROOT` environment variable

Package resolution works as follows:
1. Files in a source directory declare `package foo`
2. Other files import them via `import "foo"` (looks for `foo/foo.odin` or `foo.odin` under ODIN_ROOT or relative paths)
3. The compiler creates one LLVM module per package, resolves imports, then links them together

## Required Changes (by Phase)

### ✅ Phase 1: CLI and Environment Setup (COMPLETE)

#### 1.1 Add `ODIN_ROOT` environment variable support ✅
- **File**: `src/package_resolver.c`
- **What was done**: `resolve_odin_root()` checks `ODIN_ROOT` env var, falls back to `<exe_dir>/../..` using `realpath` to resolve symlinks. Stored in `SemContext` and `IrGenContext` via `PackageResolverContext`.

#### 1.2 Extend CLI to accept package compilation ⏳
- **Status**: `build` and `check` commands exist. `ODIN_ROOT` is resolved on startup.
- **Remaining**:
  - `-out:<path>` flag for specifying output file
  - `--emit-obj` flag
  - `--link` flag for automatic linker invocation

### ✅ Phase 2: Package/Import Semantic Handling (COMPLETE)

#### 2.1 Parse and store package name ✅
- **What was done**: `sem_pass1_register_top_level_ex` extracts package name from `AST_NODE_PACKAGE_CLAUSE`, stores in `ctx->package_name`.

#### 2.2 Resolve and parse imported files ✅
- **What was done**: Recursive import handling in `sem_pass1_register_top_level_ex`. Checks relative path, then `ODIN_ROOT/src/<path>/<path>.odin`. Creates `ImportedPackage` with its own scope, runs pass1/pass2 recursively. `push_path`/`pop_path` for cycle detection. Supports `AST_NODE_IMPORT`, `AST_NODE_IMPORT_NAMED`, `AST_NODE_IMPORT_USING`.

#### 2.3 Symbol visibility and cross-package resolution ✅
- **What was done**: Package-qualified access (`pkg.member`) handled in `AST_NODE_POSTFIX_EXPRESSION` case. `find_imported_package_by_name()` looks up imports. `op->resolved_symbol` set on `POSTFIX_MEMBER` nodes. `import using` copies symbols via `import_using_copy_symbol`.

### ✅ Phase 3: Multi-File IR Generation (COMPLETE — single module approach)

#### 3.1 Generate LLVM IR per package ✅
- **What was done**: Import processing loop in `ir_generate()` pushes each package's scope, calls `ir_gen_process_ast()`, pops scope. Procedures/constants are added to the same LLVM module. `codegen_done` flag prevents re-processing.

#### 3.2 Link packages into a single module ✅
- **What was done**: Single-module approach (Option B from decisions below). No `LLVMLinkModules2()` needed — all codegen goes into one module. `import_using_copy_symbol` re-copy loop refreshes LLVM values after import codegen.

### ⏳ Phase 4: Linker Integration

#### 4.1 Collect library dependencies from packages ❌
- **Not started**. Current approach: test script runs clang on the `.ll` file.

#### 4.2 Optional: Invoke linker directly ❌
- **Not started**. Would add `--link` flag to `main.c`, invoke clang/gcc after emission.

### ❌ Phase 5: Standard Library Stubs

#### 5.1 Builtin package ❌
- **Directory**: `odin-root/src/builtin/`
- **What's needed**: Minimal `builtin.odin` stub so `import "builtin"` resolves.

#### 5.2 Core package ❌
- **Directory**: `odin-root/src/core/`
- **Sub-packages**: `core:mem`, `core:slice`, `core:intrinsics`, `core:runtime`

#### 5.3 Vendor package ❌
- **Directory**: `odin-root/src/vendor/`
- **Sub-packages**: `vendor:glfw`, `vendor:openGL`, `vendor:raylib`, etc.

### ✅ Phase 6: Cross-Package Symbol Resolution Details (COMPLETE)

#### 6.1 Import processing during semantic analysis ✅
- Done: Recursive pass1/pass2 per package, symbols in package scope, `find_imported_package_by_name()` in `POSTFIX_EXPRESSION` handler.

#### 6.2 Circular dependency detection ✅
- Done: `import_push_path`/`import_pop_path` with stack, tested with `expected_to_fail/test_import_cycle.odin`.

#### 6.3 Package search algorithm ✅
- Done: `resolve_import_path()` in `package_resolver.c` implements the full search algorithm.
```
function resolve_import(import_path, source_file_path, odin_root):
    // 1. Check relative to source file
    candidate = join(dirname(source_file_path), import_path, import_path + ".odin")
    if exists(candidate): return candidate
    
    // 2. Check relative to source dir (flat)
    candidate = join(dirname(source_file_path), import_path + ".odin")
    if exists(candidate): return candidate
    
    // 3. Check in <ODIN_ROOT>/src/<import_path>/<import_path>.odin
    candidate = join(odin_root, "src", import_path, import_path + ".odin")
    if exists(candidate): return candidate
    
    // 4. Check in <ODIN_ROOT>/src/<import_path>.odin
    candidate = join(odin_root, "src", import_path + ".odin")
    if exists(candidate): return candidate
    
    // 5. Report error: package not found
    return NULL
```

## Implementation Status

### ✅ Milestone 1: Basic import resolution (Phases 1-2) **DONE**
- ODIN_ROOT detection, search paths, package clause, import parsing, recursive sem analysis, cycle detection, 72 tests pass.

### ✅ Milestone 2: Multi-module IR (Phase 3) **DONE**
- Single-module approach used. Imports generate IR into same module. Cross-package function calls work. `import_using_copy_symbol` re-copy loop refreshes LLVM values.

### ❌ Milestone 3: Standard library stubs (Phase 5)
1. Create minimal `builtin` package stub
2. Create minimal `core:mem` package (with Odin `foreign` to libc)
3. Create minimal `core:runtime` package
4. Test with `import "core:mem"` in an application

### ❌ Milestone 4: Linker integration (Phase 4)
1. Collect library requirements from packages
2. Implement `--link` flag to invoke clang
3. Support linking against system libraries
4. Verify end-to-end: `odinc build --link main.odin` produces a working executable

### ❌ Milestone 5: Vendor packages
1. Create vendor stubs as needed
2. Test with `vendor:glfw` or similar

## What's Next

Priority order for upcoming work:

1. **Standard library stubs (Milestone 3)** — Create `builtin` and `core` package stubs in `ODIN_ROOT/src/` directory. This enables importing standard library packages, which is needed for any real-world Odin program.
2. **Linker integration (Milestone 4)** — Add `--link` flag to automatically invoke clang after emission, producing a directly executable binary.
3. **CLI refinements (Phase 1.2)** — Add `-out:<path>`, `--emit-obj`, `--link` flags.
4. **Edge cases** — Package-level variables from imports, symbol shadowing, uppercase-export convention enforcement.
5. **Symbol name mangling** — Implement `package_name.symbol_name` mangling for external linkage to avoid collisions.

## Key Technical Decisions

### LLVM Module Strategy
- **Option A (Recommended)**: Generate one LLVM module per source file, link at end with `LLVMLinkModules2()`. Simple, works well with opaque pointers, clear isolation.
- **Option B**: Merge all ASTs into one and generate a single module. Avoids linking complexity but requires AST merging logic.

### Symbol Naming for Cross-Package References
- Use LLVM mangling: `package_name.symbol_name` for external linkage
- Convert `.` in Odin package paths to `_` in LLVM names (e.g., `core_mem_alloc`)
- This avoids collisions between packages while keeping names debuggable

### Dependency Ordering
- Topological sort based on import graph
- `builtin` is always imported first (implicitly, like Odin)
- `core:*` packages may depend on each other

## Rollback Safety
- All changes are additive: no existing single-file behavior is modified
- A file without `import` or `package` clauses compiles exactly as before
- The `build <file>` command remains unchanged; new commands can be `build-pkg <file>` or `build --pkg <file>`
- Existing test suite continues to pass unchanged

## Current File Sizes for Reference
| File | Lines | Role |
|------|-------|------|
| `src/main.c` | 265 | CLI, parse, codegen pipeline |
| `src/semantic_analyser.c` | 3336 | Type checking, resolution, import handling |
| `src/llvm_ir_generator.c` | 4965 | LLVM IR emission, import codegen |
| `src/package_resolver.c` | 81 | Import path resolution, ODIN_ROOT |
| `src/odin_grammar.gdl` | 459 | Grammar definition |
| `src/odin_grammar_ast_actions.c` | 590 | AST builder callbacks |
| `src/type_descriptors.c` | 1029 | Type system |
| `src/scope.c` | 53 | Scope/symbol table |
| `tests/run_tests.sh` | 193 | Test runner |
