# Supporting `core:fmt` — Plan and Progress

## Goal
Support `import "core:fmt"` with `fmt.println()`, `fmt.printf()` using Odin-level formatting (no C printf delegation).

## Analysis

### Current State
- Simple imports (`import "path"`) resolve relative to source dir or `<ODIN_ROOT>/src/<pkg>/`
- Collection prefix imports (`core:fmt`) working via colon-split in `resolve_import_path`
- `print_string` built-in (compiler keyword, like `len`, `cap`) available for byte/string output
- `fmt.odin` stub exists at `stubs/core/fmt/fmt.odin` with `println` using `print_string`
- Bodyless procedures (`---`) for extern declarations
- Cross-package function calls with context threading
- No escape sequence handling in string literals (`"\n"` outputs literal `\n`)

### Missing Features (by dependency order)

1. **Escape sequences** — `\n`, `\t`, `\"` etc. in string literals
2. **Variadic functions** (`..any`) — No `...` support in grammar, type system, or IR generation
3. **String formatting** — Integer-to-string, float-to-string conversion (formatting logic)
4. **`fmt.printf`** — Format string parsing with `%d`, `%s`, `%x` specifiers

## Phase Plan

### Phase 1: Collection Prefix Imports ✅ DONE
- Modified `package_resolver.c:resolve_import_path` to split on `:`
- Map `core:fmt` → `<ODIN_ROOT>/core/fmt/fmt.odin` (resolves to stubs dir)
- Built-in procedure `print_string(s: string)` via grammar keyword, AST node, semantic handler, IR generator (declares `putchar`, loops over string bytes)

### Phase 2: I/O Output Mechanism ✅ DONE
- Added `KwPrintString` lexeme, `PrintStringExpr` grammar rule, `AST_NODE_PRINT_STRING_EXPR` node
- Semantic analysis validates argument is `string` type
- IR generation extracts data ptr + len from string struct, loops calling `putchar` per byte

### Phase 3: `fmt` Package Stub ✅ DONE
- `stubs/core/fmt/fmt.odin` defines `println(s: string)` calling `print_string(s)` + `print_string("\n")`
- Cross-package calls to `fmt.println` work end-to-end
- Escape sequence handling needed for proper newline output

### Phase 4: Variadic `..any` Parameters ✅ DONE
- Added `VariadicMarker = DotDot @AST_ACTION_ELLIPSIS` grammar rule
- `Parameter` rule updated: `Identifier Colon VariadicMarker? TypePrefix`
- Semantic analyser detects `AST_NODE_ELLIPSIS` child in `AST_NODE_PARAMETER`, resolves `..any` to `[]any`
- IR generator at call site packs extra args into `[]any` slice (backing array of `any` structs)
- Distinguishes `..any` from bare `...` (C varargs) by checking last param type (slice vs non-slice)
- `LLVMFunctionType` only marked variadic for bare `...` or C convention, not `..any`
- `foo :: proc(args: ..any) -> int` and `foo(1, 2, 3)` work end-to-end

### Phase 5: String Formatting (for `printf`)
- Integer-to-string conversion in Odin
- Format specifier parser (`%d`, `%s`, `%x`, `%v`)
- `fmt.printf` procedure with format string + args

### Phase 6: Testing
- `test_fmt.odin` with `fmt.println(42)`, `fmt.printf("%d %s", 99, "hello")`
- Escape sequence tests

## Design Decisions
- No C `printf` delegation — formatting logic implemented in Odin or compiler built-ins
- Output uses `print_string` built-in (compiler generates putchar loop in LLVM IR)
- Escape sequences in string literals are a pre-existing gap (not specific to fmt)
- Variadic `..any` is a simplified form of Odin's `..T`; for now `..any` treats each arg as `any` type
- Formatting can use Odin-level integer/float-to-string routines

## Progress

- [x] Phase 1: Collection prefix imports
- [x] Phase 2: I/O output mechanism (`print_string` built-in)
- [x] Phase 3: `fmt` package stub with `println`
- [x] Phase 4: Variadic `..any` parameters
- [ ] Phase 5: String formatting
- [ ] Phase 6: Testing
