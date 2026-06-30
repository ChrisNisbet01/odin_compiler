# Package Import Support Plan

## Current State Summary

| Feature | Status | Location |
|---------|--------|----------|
| `package` clause parsing | Parsed, silently ignored | `odin_grammar.gdl:425`, no handler |
| `import "path"` parsing | Parsed, silently ignored | `odin_grammar.gdl:427-429`, no handler |
| `import name "path"` parsing | Parsed, silently ignored | `odin_grammar.gdl:428`, no handler |
| `foreign import name "lib"` | Fully implemented | IR: collects library names → LLVM metadata |
| `foreign name { decls }` | Fully implemented | Registers decls, emits code |
| Multi-file compilation | Not implemented | No package resolution or IR merging |
| `ODIN_ROOT` / search paths | Not implemented | No env var or path resolution |
| Linker invocation | Not implemented | Handled by test script `clang` call |
| Object file emission | Exists but unused | `emit_to_file()` in `llvm_ir_generator.c` |
| Symbol visibility / export | Not implemented | All symbols are file-local |

## Architecture Overview

The official Odin compiler uses `ODIN_ROOT` to locate the standard library packages:
- Default: directory containing the compiler executable (`argv[0]`-relative, `../..` from the compiler binary's location)
- Can be overridden via the `ODIN_ROOT` environment variable

Package resolution works as follows:
1. Files in a source directory declare `package foo`
2. Other files import them via `import "foo"` (looks for `foo/foo.odin` or `foo.odin` under ODIN_ROOT or relative paths)
3. The compiler creates one LLVM module per package, resolves imports, then links them together

## Required Changes (by Phase)

### Phase 1: CLI and Environment Setup

#### 1.1 Add `ODIN_ROOT` environment variable support
- **File**: `src/main.c`
- **Changes**:
  - On startup, check `getenv("ODIN_ROOT")`
  - Fall back to a path relative to `argv[0]` (like `dirname(argv[0])/../..`)
  - Store the resolved root path for later use
  - Add `--odin-root` / `-root` CLI flag to override

#### 1.2 Extend CLI to accept package compilation
- **File**: `src/main.c`
- **Changes**:
  - Add `build-pkg <package-path>` or extend `build` with flags
  - Accept optional output file path (currently hardcoded to `<input>.ll`)
  - Add `-out:<path>` flag for specifying output
  - Add `--emit-obj` flag to use `emit_to_file()` for object file output
  - Add `--link` flag to optionally invoke the linker

### Phase 2: Package/Import Semantic Handling

#### 2.1 Parse and store package name
- **File**: `src/semantic_analyser.c`
- **Changes**:
  - In pass 1 (around line 2291), add `case AST_NODE_PACKAGE_CLAUSE:` handler
  - Extract the package name from children/identifier text
  - Store the package name in the `SemContext` (add a `package_name` field)
  - Later, this will identify the package for cross-package symbol resolution

#### 2.2 Resolve and parse imported files
- **File**: `src/semantic_analyser.c` (new functions)
- **Changes**:
  - In pass 1, add `case AST_NODE_IMPORT:` and `case AST_NODE_IMPORT_NAMED:` handlers
  - For `import "path"`:
    1. Search for the imported file: check relative to source file, then `ODIN_ROOT/src/<path>/<path>.odin`
    2. Read and parse the imported file (reuse `read_file` + `epc_parse_str` + `epc_ast_build`)
    3. Store the imported AST root for later analysis
  - For `import name "path"`:
    - Same file resolution, but register the imported symbols under `name`
  - Add package search paths to `SemContext`:
    ```c
    typedef struct {
        // ... existing fields ...
        char const * package_name;
        char const * source_dir;     // dirname of the main source file
        char const * odin_root;      // from env/CLI
        imported_package_t * imports; // resolved imported packages
        int import_count;
    } SemContext;
    ```

#### 2.3 Symbol visibility and cross-package resolution
- **File**: `src/scope.c`, `src/semantic_analyser.c`
- **Changes**:
  - Define package-level scope for each compiled file
  - When processing `import "pkg"`:
    1. Parse and semantically analyse the imported file's package-level declarations
    2. Import all **exported** symbols from that package into the current file's scope
    3. "Exported" in Odin means symbols starting with an uppercase letter (convention, currently not enforced)
  - Multi-pass:
    - Pass 1 per file: register all top-level decls in the file's own package scope
    - Pass 2 per file: for each import, merge exported symbols into the current scope
    - Pass 3: analyse function bodies

### Phase 3: Multi-File IR Generation

#### 3.1 Generate LLVM IR per package
- **File**: `src/llvm_ir_generator.c`, `src/main.c`
- **Changes**:
  - For each parsed package file, generate LLVM IR into a separate LLVM module
  - Collect all modules in `IrGenContext`
  - Add module-level metadata for each module's package name
  - Handle cross-module symbol references (use LLVM linkage: `external` for exported symbols, `internal`/`private` for non-exported)

#### 3.2 Link packages into a single module
- **File**: `src/llvm_ir_generator.c` or `src/main.c`
- **Changes**:
  - After generating IR for all packages, use `LLVMLinkModules2()` to merge them into one
  - Resolve cross-module symbol references
  - Emit the final merged module to `.ll` or `.o`

### Phase 4: Linker Integration

#### 4.1 Collect library dependencies from packages
- **File**: `src/llvm_ir_generator.c`
- **Changes**:
  - When processing imports that are standard library packages (`core/*`, `vendor/*`, `builtin`),
    automatically link against the required system libraries (e.g., `libc`, `libm`, `pthread` for `core:sys`)
  - Add known library mappings for Odin packages:
    ```
    builtin → (internal, no external lib)
    core:mem → (internal)
    core:sys → -lpthread (on Linux)
    vendor:glfw → -lglfw
    vendor:openGL → -lGL
    ```

#### 4.2 Optional: Invoke linker directly
- **File**: `src/main.c`
- **Changes**:
  - Add `-link` flag
  - When set, after emitting `.o` or `.ll`, invoke clang/gcc to produce executable:
    ```c
    // Build linker command:
    // clang <output.o> -o <output.exe> -lpthread -lm [...]
    ```
  - Use `fork()`/`exec()` or `system()` — simple approach matching the test script pattern

### Phase 5: Standard Library Stubs (builtin, core, vendor)

#### 5.1 Builtin package
- **Directory**: `odin-root/src/builtin/`
- **Contents**: Minimal builtin package with the absolute essentials
  - `builtin.odin` — `package builtin`
  - Should define `int`, `float`, `bool`, `string`, `rawptr`, etc. (these are already hardcoded in the compiler's type system, so no code is strictly needed — but having stub files means `import "builtin"` resolves cleanly)
  - The type system's `register_builtin_context_types` already handles `Context` and `Allocator` structs

#### 5.2 Core package
- **Directory**: `odin-root/src/core/`
- **Sub-packages**:
  - `core:mem` — memory allocation (`alloc`, `free`, `resize`)
  - `core:slice` — slice operations (`append`, `delete`)
  - `core:intrinsics` — LLVM intrinsics (`volatile_load`, `atomic_add`, etc.)
  - `core:runtime` — runtime startup, memory init
- **Initial implementation**:
  - Create stub `.odin` files with the minimal type signatures
  - Implement a few key functions in Odin or directly in the IR generator
  - Many `core` functions can be trivially implemented (e.g., `len`, `cap` are already builtins)

#### 5.3 Vendor package
- **Directory**: `odin-root/src/vendor/`
- **Sub-packages**: `vendor:glfw`, `vendor:openGL`, `vendor:raylib`, etc.
- **Initial implementation**:
  - Stub files with foreign import declarations (matching real C library ABIs)
  - Users can override/expand as needed

### Phase 6: Cross-Package Symbol Resolution Details

#### 6.1 Import processing during semantic analysis
- The semantic analyser currently processes one file. To handle imports:
  1. Before pass 1, collect all `import` declarations from the AST
  2. For each import, resolve the file path, read and parse the file
  3. Run pass 1 on the imported file's AST (register all symbols in its package scope)
  4. Create a mapping of package_name → scope with exported symbols
  5. In pass 1 of the importing file, import the exported symbols into the current scope
  6. Run pass 2 on all files in dependency order

#### 6.2 Circular dependency detection
- Track which packages are currently being processed
- If a package is encountered twice in the dependency chain, report an error (Odin does not allow circular imports between packages)

#### 6.3 Package search algorithm
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

## Implementation Order (Recommended)

### Milestone 1: Basic import resolution (Phases 1-2)

1. Add `ODIN_ROOT` detection to CLI
2. Add import search path resolution function
3. Add `package_name` to `SemContext`, handle `AST_NODE_PACKAGE_CLAUSE`
4. Handle `AST_NODE_IMPORT` by reading and parsing the imported file
5. Chain semantic analysis across imports (recursive pass 1 for all deps)
6. Create a `--check-pkg` command that type-checks a package and all its imports

### Milestone 2: Multi-module IR (Phase 3)

1. Generate a separate LLVM module per package file
2. Link modules with `LLVMLinkModules2()`
3. Handle symbol visibility (public vs private)
4. Test with a two-file example

### Milestone 3: Standard library stubs (Phase 5)

1. Create minimal `builtin` package stub
2. Create minimal `core:mem` package (with Odin `foreign` to libc)
3. Create minimal `core:runtime` package
4. Test with `import "core:mem"` in an application

### Milestone 4: Linker integration (Phase 4)

1. Collect library requirements from packages
2. Implement `--link` flag to invoke clang
3. Support linking against system libraries
4. Verify end-to-end: `odinc build --link main.odin` produces a working executable

### Milestone 5: Vendor packages

1. Create vendor stubs as needed
2. Test with `vendor:glfw` or similar

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
| `src/semantic_analyser.c` | 2878 | Type checking, resolution |
| `src/llvm_ir_generator.c` | 4916 | LLVM IR emission |
| `src/odin_grammar.gdl` | 459 | Grammar definition |
| `src/odin_grammar_ast_actions.c` | 590 | AST builder callbacks |
| `src/type_descriptors.c` | 1029 | Type system |
| `src/scope.c` | 53 | Scope/symbol table |
| `tests/run_tests.sh` | 193 | Test runner |
