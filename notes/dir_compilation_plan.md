# Directory Compilation Plan — Option B (AST Merging)

## Overview

Currently `odinc` takes a single `.odin` file. We want to support `odinc build my_package/` where `my_package/` contains multiple `.odin` files that share a `package` clause.

The approach: parse each `.odin` file independently, then **merge the ASTs** into a single `Program` tree. The rest of the compiler (semantic analysis, IR generation) sees one unified AST and works unchanged.

---

## AST structure (current)

After parsing a single file, the structure is:

```
Program [AST_NODE_PROGRAM]
  └── child[0]: ExternalDeclarations [AST_NODE_EXTERNAL_DECLARATIONS]
        ├── child[0]: PackageClause ("main")
        ├── child[1]: Import ("core:fmt")
        ├── child[2]: ConstantDecl (foo :: 42)
        ├── child[3]: ConstantDecl (bar :: proc() { ... })
        └── ...
```

Both `sem_pass1_register_top_level_ex()` and `sem_pass2_analyse_bodies_ast()` iterate `program->list.children` looking for `AST_NODE_EXTERNAL_DECLARATIONS`, then iterate that node's children for top-level decls. Same pattern in `ir_gen_process_ast()`.

---

## Merged AST (target)

```
Program [AST_NODE_PROGRAM]                              ← newly created
  └── child[0]: ExternalDeclarations [AST_NODE_EXTERNAL_DECLARATIONS]  ← newly created
        ├── child[0]: PackageClause ("main")              ← from file A
        ├── child[1]: Import ("core:fmt")                 ← from file A (deduped)
        ├── child[2]: ConstantDecl (foo :: 42)            ← from file A
        ├── child[3]: ConstantDecl (helper :: proc...)    ← from file B
        ├── child[4]: Import ("core:os")                  ← from file B
        ├── child[5]: ConstantDecl (bar :: proc() {...})  ← from file C
        └── ...
```

Children are **moved** (pointer stolen) from each file's AST, not copied. The wrapper nodes (`Program`, `ExternalDeclarations`) from each file are freed after their children are detached.

---

## Implementation steps

### 1. Parse result type

Add a struct to hold a parsed file's output alongside its metadata:

```c
typedef struct {
    char * source_path;          // full path (for error reporting)
    char * source_text;          // file text (owned)
    odin_grammar_node_t * ast;   // full program AST (owned)
    char * package_name;         // extracted from PackageClause
} ParsedFile;
```

### 2. Single-file parser function

Extract the read+parse+AST-build logic from `main.c` into a reusable function:

```c
ParsedFile * parse_source_file(char const * path, epc_parser_t * parser,
                                epc_ast_hook_registry_t * hooks);
```

This is essentially what `parse_imported_file()` does, but returns a `ParsedFile` instead of `ImportedPackage`. It reads the file, parses it (`epc_parse_str`), builds the AST (`epc_ast_build`), extracts the package name from `AST_NODE_PACKAGE_CLAUSE`, and returns the result.

**Refactoring opportunity**: `parse_imported_file()` can be rewritten to delegate to `parse_source_file()`, then wrap the result in an `ImportedPackage`.

### 3. AST merge function

Signature:

```c
odin_grammar_node_t * merge_program_asts(ParsedFile ** files, int file_count,
                                          char const * source_dir_for_errors,
                                          char ** out_error);
```

Logic:

1. Create a new `AST_NODE_EXTERNAL_DECLARATIONS` node (empty children list)
2. Track that we've seen one `PackageClause` — only insert the first one, validate subsequent ones match
3. For each file's AST:
   a. Locate `AST_NODE_EXTERNAL_DECLARATIONS` child of the `AST_NODE_PROGRAM`
   b. For each child of that `ExternalDeclarations` node:
      - If `PackageClause`: validate name matches, skip if already seen
      - If `Import` / `ImportNamed` / `ImportUsing`: check dedup set, add if new
      - Else: add to the merged ExternalDeclarations
4. After extracting, null-out the source `ExternalDeclarations` children (so their ASTs can be freed safely)
5. Free each file's `Program` + `ExternalDeclarations` wrapper nodes (the inner children are now owned by the merged tree)
6. Create `AST_NODE_PROGRAM` with the merged `ExternalDeclarations` as its sole child
7. Return the new program AST

**Ownership model**:
- Each file's `ExternalDeclarations` children are **moved** (pointer transferred) to the merged tree
- The `Program` and `ExternalDeclarations` wrapper nodes from each file are freed (after detaching children)
- The merged `Program` owns all top-level decls

### 4. `file_path` field on every AST node (Option B)

Add a `char * file_path` field to `odin_grammar_node_t` so every node remembers which source file it came from. Updated in `odin_grammar_node_free` to free the string.

**In `parse_source_file()`**: After AST build, walk the tree and set `node->file_path = strdup(path)` on every node.

**Effect on error reporting**: `sem_error_list_add()` and `ir_gen_error_collection_add()` are updated to use `node->file_path` when the passed `file_path` is NULL. This lets call sites pass NULL and get the correct per-file path automatically. The `ctx->source_file_path` field is retained for early errors (before node creation).

### 5. CLI change (`main.c`)

Current signature:
```
odinc build [-o <output>] [--keep-temps] <file>
```

New signature:
```
odinc build [-o <output>] [--keep-temps] [--file <file>] <path>
```

Where `<path>` is auto-detected:
- If `path` is a directory (`S_ISDIR`): compile all `.odin` files in the directory
- If `path` is a file: compile as single file (existing behavior)
- `--file <path>`: force single-file mode regardless of path type

Logic at filename handling point (line 125–156):

1. Check for `--file <path>` flag → force single-file mode
2. Otherwise, `stat(argv[i])` + `S_ISDIR()` check on the positional argument
3. If directory:
   a. `opendir`/`readdir` to enumerate all `*.odin` files
   b. Filter out temp/backup files
   c. Parse each file in parallel (pthreads)
   d. Merge ASTs via `merge_program_asts()`
   e. Set `source_dir = dir_path`
4. If file:
   a. Existing single-file path (no change)

### 6. Semantic analysis changes

`SemContext` already takes a single `ast` — pass the merged AST. No changes to semantic analysis internals.

**Error reporting for merged AST**: Each node carries its own `file_path` (set during parsing in step 4). Error call sites pass NULL for file_path; the error functions use `node->file_path` automatically. This means `ctx->source_file_path` is no longer needed for error reporting in most cases — only for early errors that don't have a node.

### 7. IR generator changes

`ir_generate()` already takes a single `ast` — pass the merged AST. No changes to `ir_gen_process_ast()` or `ir_gen_context_create()`.

**Error reporting**: Same pattern — IR gen error calls pass NULL for file_path; the function extracts it from `node->file_path`.

### 6. IR generator changes

`ir_generate()` already takes a single `ast` — pass the merged AST. No changes to `ir_gen_process_ast()` or `ir_gen_context_create()`.

### 7. Test

Create `tests/multi_file_pkg/` with:

**`tests/multi_file_pkg/package.odin`:**
```odin
package multi_file_test

import "core:fmt"

main :: proc() {
    fmt.println(helper_func())
    fmt.println(helper_constant)
}
```

**`tests/multi_file_pkg/helper.odin`:**
```odin
package multi_file_test

helper_func :: proc() -> int {
    return 42
}
```

**`tests/multi_file_pkg/constants.odin`:**
```odin
package multi_file_test

helper_constant :: 100
```

**Test runner change** (`tests/run_tests.sh`):
All existing tests use file paths (e.g., `tests/test_for.odin`). With auto-detect, these still work unchanged — `stat()` says they're files, so single-file mode is used. No changes to how existing tests are invoked.

For directory tests, add a new `run_dir_test()` function:
```bash
run_dir_test() {
    local odin_dir="$1"
    local dir_name=$(basename "$odin_dir")
    local exe_file="$OUTPUT_DIR/${dir_name}"
    # Invoke with directory path
    "$COMPILER" build "$odin_dir"
    # ... same execution/check logic as run_test
}
```

If `SPECIFIC_TEST` ends with `/`, treat it as a directory test. Auto-discovery also checks for directory tests via `find -maxdepth 1 -type d` and checks for a sentinel file like `.odin_pkg` to identify test packages.

---

## Parallel parsing

Since parsing each file is fully independent (separate read + parse + AST build), we can parallelize with **pthreads**.

Implementation sketch:

```c
typedef struct {
    char const * file_path;
    epc_parser_t * parser;
    epc_ast_hook_registry_t * hooks;
    ParsedFile * result;
    char * error;
} ParseJob;

static void * parse_job_thread(void * arg) {
    ParseJob * job = (ParseJob *)arg;
    job->result = parse_source_file(job->file_path, job->parser, job->hooks);
    return NULL;
}

// Usage:
ParseJob jobs[file_count];
pthread_t threads[file_count];

for (int i = 0; i < file_count; i++) {
    jobs[i] = (ParseJob){
        .file_path = odin_files[i],
        .parser = create_odin_grammar_parser(list),  // one parser per thread
        .hooks = shared_hooks,  // hooks is read-only after init
    };
    pthread_create(&threads[i], NULL, parse_job_thread, &jobs[i]);
}

for (int i = 0; i < file_count; i++) {
    pthread_join(threads[i], NULL);
    // collect results
}
```

**Notes**:
- `epc_parser_t` should be thread-safe for concurrent `epc_parse_str` calls (grammar tables are read-only). One parser per thread eliminates any contention.
- `epc_ast_hook_registry_t` is read-only after `odin_grammar_ast_hook_registry_init()` — safe to share across threads.
- Each thread gets its own parser via `create_odin_grammar_parser()` — parser creation is fast since grammar rules are already compiled into the binary.

**Performance**: For `N` files, wall time ≈ `max_i(parse_time_i)` instead of `sum_i(parse_time_i)`. Practically, parsing is I/O-bound for small files, so the speedup may be limited. But for packages with many files, the savings add up.

---

## Import code reuse

### `parse_imported_file()` refactoring

Currently `parse_imported_file()` duplicates the read+parse+AST-build logic. We can refactor it to use `parse_source_file()` internally:

```c
ImportedPackage * parse_imported_file(char const * file_path, ...) {
    ParsedFile * pf = parse_source_file(file_path, parser, hooks);
    if (pf == NULL) return NULL;
    
    ImportedPackage * pkg = calloc(1, sizeof(ImportedPackage));
    pkg->source_path = pf->source_path;    // transfer ownership
    pkg->source_dir = extract_dir(pf->source_path);
    pkg->source_text = pf->source_text;    // transfer ownership
    pkg->ast = pf->ast;                    // transfer ownership
    pkg->package_name = pf->package_name;  // transfer ownership
    // pkg->package_scope set later by semantic analyser
    // pkg->analysed = false by default
    
    free(pf);  // just the struct, inner pointers are transferred
    return pkg;
}
```

### Multi-file import packages

Currently `resolve_import_path()` resolves to a **single file** using the `<pkg>/<pkg>.odin` convention. For packages with multiple `.odin` files:

```c
// New: resolve all .odin files in a package directory
char ** resolve_import_path_multi(char const * import_name, char const * source_dir,
                                   char const * odin_root, int * out_count);
```

This would:
1. Try each resolution path as before
2. If a directory exists at the resolved path, enumerate all `.odin` files inside
3. Return the list of file paths

Then `sem_pass1_register_top_level_ex()` would parse all files in the imported package, merge their ASTs, and run semantic analysis on the merged tree.

**Potential issues**:
- Import cycle detection needs to track directories, not file paths
- Package clause validation across files (must all match)
- Error reporting needs to include the correct file path

This is a **future enhancement** — not needed for the initial directory compilation feature.

---

## Summary of files to change

| File | Change |
|------|--------|
| `src/main.c` | Detect directory argument, enumerate `.odin` files, call parse + merge, pass merged AST |
| `src/main.c` (or new) | `parse_source_file()` function |
| `src/main.c` (or new) | `merge_program_asts()` function |
| `src/main.c` | Parallel parsing with pthreads |
| `src/package_resolver.c` | Refactor `parse_imported_file()` to share `parse_source_file()` |
| `tests/run_tests.sh` | Add directory-test support |
| `tests/multi_file_pkg/` | Test files (3 `.odin` files) |
| `src/odin_grammar_ast.h` | Add `char * file_path` field to `odin_grammar_node_t` |
| `src/odin_grammar_ast_actions.c` | Free `n->file_path` in `odin_grammar_node_free` |
| `src/sem_error.c` / `src/ir_gen_error.c` | Fall back to `node->file_path` when file_path arg is NULL |
| `src/semantic_analyser.c` | Pass NULL for file_path at all error call sites (or pass `node->file_path`) |
| `src/llvm_ir_generator.c` | Pass NULL for file_path at all error call sites |

**No changes needed to**:
- `src/odin_grammar.gdl`

---

## Verification plan

1. **Single-file path unchanged**: `odinc build tests/test_for.odin` still works (regression)
2. **Directory compilation**: `odinc build tests/multi_file_pkg/` produces correct output
3. **Error reporting**: Package name mismatch across files produces a clear error
4. **Parallel parsing**: 3-file package parses correctly
5. **Import refactoring**: All existing import tests still pass
6. **Full test suite**: All 109 existing tests pass
