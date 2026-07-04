# Supporting `core:fmt` — Plan and Progress

## Goal
Support `import "core:fmt"` with `fmt.println()`, `fmt.printf()` using Odin-level formatting (no C printf delegation).

## Analysis

### Current State
- Simple imports (`import "path"`) resolve relative to source dir or `<ODIN_ROOT>/src/<pkg>/`
- Foreign function binding (`foreign libc { ... }`) with `-l` linker flags
- Bodyless procedures (`---`) for extern declarations
- Cross-package function calls with context threading
- No `:)` collection prefix support in import resolver

### Missing Features (by dependency order)

1. **Collection prefix imports** (`core:fmt`) — `resolve_import_path` has no concept of `collection:package` syntax
2. **Variadic functions** (`...`) — No `...` support in grammar, type system, or IR generation
3. **I/O output mechanism** — No way to write bytes to stdout without C FFI
4. **`fmt` package** — No `stubs/src/fmt/fmt.odin` exists
5. **String formatting** — Integer-to-string, float-to-string conversion (formatting logic)

## Phase Plan

### Phase 1: Collection Prefix Imports
- Modify `package_resolver.c:resolve_import_path` to split on `:`
- Map collection names to directories:
  - `core` → `<ODIN_ROOT>/core/` (project root)
- Resolve `core:fmt` → `<ODIN_ROOT>/core/fmt/fmt.odin`

### Phase 2: Variadic `...` Parameters
- Add `KW_Ellipsis` lexeme (or reuse existing `AST_NODE_ELLIPSIS`)
- Add grammar rule: `VariadicParam = Ellipsis TypePrefix` or similar
- Add `AST_NODE_VARIADIC_PARAM` / flag on `ProcType`
- Handle in semantic analyser (variadic parameter resolution)
- Handle in IR generator (LLVM varargs function types, `..."c"` calling convention)

### Phase 3: I/O Output Mechanism
- Add built-in `print`/`println` support in the compiler (like `len`, `cap`)
- Or add foreign function binding for `write` syscall (minimal OS layer)
- Or add compiler intrinsics for raw byte/string output

### Phase 4: `fmt` Package Implementation
- Create `stubs/core/fmt/fmt.odin` with:
  - `print(things: ..any)` — format and output each arg
  - `println(things: ..any)` — like print with trailing newline
  - `printf(fmt: string, args: ..any)` — format string with specifiers
  - Internal formatting logic: `int_to_string`, `float_to_string`, etc.
- Format specifier support: `%d`, `%f`, `%s`, `%x`, `%v`, etc.

### Phase 5: Testing
- `test_fmt.odin` with `fmt.println(42)`, `fmt.printf("%d %s", 99, "hello")`

## Design Decisions
- No C `printf` delegation — formatting logic implemented in Odin or compiler built-ins
- Output mechanism needs to write bytes; easiest path: add compiler built-in `__print_string` / `__print_byte`
- Variadic `..any` is a simplified form of Odin's `..T`; for now `..any` treats each arg as `any` type
- Formatting can use Odin-level integer/float-to-string routines

## Progress

- [ ] Phase 1: Collection prefix imports
- [ ] Phase 2: Variadic `...` parameters
- [ ] Phase 3: I/O output mechanism
- [ ] Phase 4: `fmt` package
- [ ] Phase 5: Testing
