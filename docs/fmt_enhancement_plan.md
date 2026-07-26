# Core:fmt Enhancement Plan

## Current Status
All 191 tests pass. The fmt module supports basic printing, format specifiers, strings.Builder-based output, and cross-package imports.

## Implemented Features
- `println`, `printf`, `printfln`, `eprintln`, `eprintf`, `eprintfln`
- Format specifiers: `%d`, `%s`, `%x`, `%u`, `%f`, `%v`, `%%`, `%b`, `%o`, `%X`
- Type support: int, i8, i16, i32, i64, u8, u16, u32, u64, uintptr, rune, byte, f32, f64, bool, string
- Runtime type identification via `type_of(v)` for `any` type
- `strings.Builder` support in `core:strings` (builder_make, write_byte, write_string, to_string, to_bytes)
- Builder-based formatted output: `sbprint`, `sbprintf`, `sbprintln`, `sbprintfln`
- Cross-package `core:strings` import from `core:fmt`

## Completed Work

### fmt Module Extensions (Completed 2026-07-25)
- [x] Added `%b` binary format specifier
- [x] Added `%o` octal format specifier  
- [x] Added `%X` uppercase hex format specifier
- [x] Implemented `print_binary()` helper function
- [x] Implemented `print_octal()` helper function
- [x] Implemented `print_hex_upper()` helper function

### String Builder Support (Completed 2026-07-25)
- [x] Created `stubs/core/strings/strings.odin` with Builder struct
- [x] Implemented `builder_make_none()`, `builder_make(n)`
- [x] Implemented `write_byte()`, `write_bytes()`, `write_string()`
- [x] Implemented `to_string()`, `to_bytes()` as builtins
- [x] Fixed IR generation for empty struct literals (returns zero-initialized values)
- [x] Fixed append() to use select-based conditional growth (avoids LLVM crashes)

### Qualified Type Name Support (Completed 2026-07-26)
- [x] Added `QualifiedTypeName` grammar rule
- [x] Added `AST_NODE_QUALIFIED_TYPE_NAME` AST node
- [x] Added semantic resolver `sem_resolve_qualified_type_name()`
- [x] All 191 tests pass
- [x] Works correctly in function parameter types (e.g., `proc(b: ^strings.Builder)`)

### Nested Import IR Gen Fix (Completed 2026-07-27)
- [x] Fixed `sem_evaluate_expr.c:1734`: Non-polymorphic package-qualified CALL nodes now propagate `resolved_symbol` from the preceding MEMBER node
- [x] Fixed `ir_gen_postfix.c:604-624`: Forward-declare cross-package procedures via `LLVMGetNamedFunction`/`LLVMAddFunction` when `symbol->value.value` is NULL
- [x] `fmt.odin` can now `import "core:strings"` and call `strings.write_string()` internally
- [x] Detailed analysis in `notes/nested_import_ir_gen_bug.md`

### Builder-based fmt Functions (Completed 2026-07-27)
- [x] Implemented `sbprint(b: ^Builder, args: ..any) -> int` — space-separated args into builder
- [x] Implemented `sbprintf(b: ^Builder, format: string, args: ..any) -> int` — formatted output into builder
- [x] Implemented `sbprintln(b: ^Builder, args: ..any) -> int` — space-separated args + trailing newline
- [x] Implemented `sbprintfln(b: ^Builder, format: string, args: ..any) -> int` — formatted output + trailing newline
- [x] Builder-aware helpers: `sb_print_string`, `sb_print_byte`, `sb_print_value`, `sb_print_int`, `sb_print_f64`, `sb_print_hex`, `sb_print_hex_upper`, `sb_print_binary`, `sb_print_octal`
- [x] All 191 tests pass (test_fmt_sb.odin covers all 4 functions with 15 subtests)

## Pending Enhancements

### Priority 1: Essential Missing Features
- [ ] Width and precision formatting (e.g., `%10d`, `%.2f`)
- [ ] Left/right alignment (`-` for left, `0` for zero padding)
- [ ] Sign flags (`+` for always show sign, ` ` for space)
- [ ] Scientific notation (`%e`, `%E`)
- [ ] General format (`%g`, `%G`)

### Priority 2: Advanced Format Features
- [ ] Python-like syntax (`{}`, `{0:d}`, `{:6.2f}`)
- [ ] Positional arguments (`%[0]d`, `%[1]s`)
- [ ] Named arguments (`{name}`)

### Priority 3: Complex Type Support
- [ ] Memory formatting (`%m`, `%M`)
- [ ] Complex number formatting
- [ ] Quaternion formatting
- [ ] Enum name formatting
- [ ] Struct field formatting with names
- [ ] Union formatting
- [ ] Matrix formatting

### Priority 4: Extensibility
- [ ] Custom formatter registration (`@(builtin)` attribute support)

## Remaining Builder Variants

### Priority 2: Builder Variants
- [ ] `aprint`, `aprintln`, `aprintf`, `aprintfln` (allocate string)
- [ ] `tprint`, `tprintln`, `tprintf`, `tprintfln` (temp allocator)
- [ ] `bprint`, `bprintfln`, `bprintf`, `bprintfln` (buffer-based)
- [ ] `caprint`, `caprintfln`, `caprintf`, `caprintfln` (C string)
- [ ] `wprint`, `wprintln`, `wprintf`, `wprintfln` (io.Writer)

### Priority 3: IO Writer Support
- [ ] Create `stubs/core/io/io.odin`
- [ ] Define `Writer`, `Reader`, `Stream_Mode`, `Error` types
- [ ] Implement `write_byte`, `write_string`, `flush`

## Estimated Effort
- Remaining format specifiers: 1-2 days
- Builder variants: 1 day
- IO Writer support: 1-2 days
- Complex type formatting: 2-3 days
- Custom formatters: 1-2 days

Total: 6-10 days for complete implementation
