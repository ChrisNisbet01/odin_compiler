# Core:fmt Enhancement Plan

## Current Status
All 193 tests pass. The fmt module supports basic printing, full format specifier handling with flags/width/precision (via `sb_format_parsed`), strings.Builder-based output, cross-package imports, and `..any` variadic forwarding.

## Compiler Bugs Fixed During This Work

### Pre-existing Limitations (Still Open)

- **Two-step Builder init pattern**: `b: pkg.Type` then `b = expr` was previously broken due to AST_NODE_QUALIFIED_TYPE_NAME bug. Now fixed. The one-step form (`b := expr`) also works and is preferred.
- **Pre-existing `append` codegen bug**: `append` on pre-allocated dynamic arrays (`make([dynamic]T, N)` followed by `append`) miscomputes the buffer pointer. Workaround: use `builder_make_none()` instead of `builder_make(N)`.
- **String equality comparison**: Strings (`{ptr, i64}` aggregate) cannot be compared with `==`. Must compare `len()` and individual characters.
- **`rawptr` not usable in source code**: The internal `ptr_type` isn't in the `BasicType` grammar rule, so `rawptr` can't be used as a variable type.

## Implemented Features

### Core Print Functions
- `println`, `printf`, `printfln`, `eprintln`, `eprintf`, `eprintfln` — basic versions with ad-hoc format parsing
- All handle: `%d`, `%s`, `%x`, `%u`, `%f`, `%v`, `%%`, `%b`, `%o`, `%X` (no width/precision/flags yet)

### Builder-based Output (`sb*` family)
- `sbprint`, `sbprintf`, `sbprintln`, `sbprintfln` — Builder variants of the print family
- `sb_print_string`, `sb_print_byte`, `sb_print_value`, `sb_print_int`, `sb_print_f64`, `sb_print_hex`, `sb_print_hex_upper`, `sb_print_binary`, `sb_print_octal`
- `sb_print_padded_string`, `sb_print_padded_char`, `sb_print_hex_lower_padded`, `sb_print_repeat`
- `flush_to_fd` — write builder contents to a file descriptor

### Allocate-based Print
- `aprint`, `aprintln` — return allocated string (uses `builder_make_none()` to avoid pre-existing append codegen bug)
- `aprintf`, `aprintfln` — formatted versions that forward `..any` to `sb_format_parsed` (was broken by `..any`→`..any` forwarding bug, now fixed)

### Format Flag Constants
- `FLAG_LEFT_ALIGN=1`, `FLAG_ALWAYS_SIGN=2`, `FLAG_SPACE_SIGN=4`, `FLAG_ZERO_PAD=8`, `FLAG_ALTERNATE=16`

### Comprehensive Format Parser: `sb_format_parsed`
- **Public API**: `sb_format_parsed(b: ^Builder, format: string, args: ..any) -> int` — direct caller, auto-packs `..any` args via compiler magic.
- **Internal**: `sb_format_parsed_inner(b: ^Builder, format: string, args: []any) -> int`. Direct `[]any` form lets `..any` callers forward `args` (a slice) directly without re-packing.
- **Supported format specifiers**:
  - `%d` — signed decimal integer
  - `%s` — string (with width/flags padding)
  - `%x` / `%X` — hex (lowercase / uppercase; supports FLAG_ALTERNATE for 0x/0X prefix)
  - `%u` — unsigned decimal
  - `%b` — binary (FLAG_ALTERNATE adds 0b/0B prefix)
  - `%o` — octal (FLAG_ALTERNATE adds 0 prefix)
  - `%c` — single character (byte/u8/int/rune; with width/flags padding)
  - `%f` / `%F` — float with fixed-point notation (default precision 6)
  - `%e` / `%E` — scientific notation (mantissa.e±XX form)
  - `%g` / `%G` — general format (auto-selects between %f and %e)
  - `%p` — pointer (0x + lower-hex of int/uintptr/u64)
  - `%v` — generic value (delegates to `sb_print_value`)
  - `%%` — literal percent
  - Unknown specifiers are printed literally
- **Flags**: `-` (left align), `+` (always sign), ` ` (space sign), `0` (zero pad), `#` (alternate form)
- **Width**: decimal integer (e.g. `%5d`)
- **Precision**: `.` followed by decimal (e.g. `%.2f`; default -1 meaning "use spec default")

### Helper Formatters
- `sb_format_int(b, v: any, base, upper, width, precision, flags, is_unsigned)` — comprehensive integer formatting with sign/zero padding, base prefixes, precision
- `sb_format_f64(b, v: any, width, precision, flags)` — float with sign/precision
- `sb_format_scientific(b, v: any, upper, width, precision, flags)` — scientific notation
- `sb_format_general(b, v: any, upper, width, precision, flags)` — general float format
- `sb_print_f64_raw(b, v: f64, precision)` — raw float printing without padding (used by other helpers)

### Type Support
- int, i8, i16, i32, i64, u8, u16, u32, u64, uintptr, rune, byte, f32, f64, bool, string
- Runtime type identification via `type_of(v)` for `any` type

### Strings.Builder Support
- `Builder` struct in `core:strings`
- `builder_make_none()`, `builder_make(n)`, `builder_cap`, `builder_space`, `reset`, `grow`
- `write_byte`, `write_bytes`, `write_string`
- `to_string`, `to_bytes` (builtins)
- `destroy` (frees the dynamic-array buffer)
- Cross-package import working; package-qualified type names work in declarations (`b: strings.Builder`)

## Pending Refactoring Work

### HIGH: Migrate `printf`/`printfln`/`eprintf`/`eprintfln`/`sbprintf`/`sbprintfln` to `sb_format_parsed`
**Status**: not started; previously blocked by `..any`→`..any` forwarding bug (now fixed).
**Plan**:
- [ ] Replace each ~80-line ad-hoc parser in `printf`/`printfln`/`eprintf`/`eprintfln` with:
  ```odin
  printf :: proc(format: string, args: ..any) {
      b := strings.builder_make_none()
      sb_format_parsed(&b, format, args)
      flush_to_fd(&b, 1)
  }
  ```
  (Use `flush_to_fd` to write the built-up string; or inline `strings.to_string` + `print_string`.)
- [ ] Same for `sbprintf`/`sbprintfln` — they already use a builder:
  ```odin
  sbprintf :: proc(b: ^Builder, format: string, args: ..any) -> int {
      return sb_format_parsed(b, format, args)
  }
  ```
- [ ] Verify all existing tests still pass with the once only 7 format specifiers (`%d`, `%s`, `%x`, `%u`, `%f`, `%v`, `%%`); then verify new specifiers/flags work transparently in `printf`.
- [ ] Delete the now-redundant ad-hoc parser bodies (saves ~480 lines of duplicate code).

### MEDIUM: Add tests for new format specifiers/flags using the migrated `printf`
- [ ] Test `printf("%c\n", 'A')`, `printf("%.3f\n", 3.14159)`, `printf("%05d\n", 42)`, `printf("%-10s|\n", "hi")`, `printf("%e\n", 123.456)`, `printf("%#x\n", 255)`.
- [ ] Test edge cases: empty args forwarded, multi-arg mixed types, precision on strings (`%.3s` truncation).

### LOW: Tag unused printf-side helpers for cleanup
- `print_value`, `print_f64`, `print_hex`, `print_hex_upper`, `print_binary`, `print_octal` — used by existing `println`/`eprintln`. After `printf` migration these may become dead code (verify; possibly remove or keep for the no-format-string value-paths).

## Pending New Features

### Priority 1: Remaining Format Features
- [ ] Precision-only specifier `%.5s` for string truncation (slicing required — easy).
- [ ] `%q` — Quoted string ("...").
- [ ] `%m` — Quotient-remainder format (`a mod b = r`).
- [ ] Field/access position argument: `%[1]d` / `%[2]s` — requires AST_NODE for spec parsing.

### Priority 2: Builder Variants
- [x] **Temp allocator infrastructure** — Implemented!
  - Added `TD_KIND_ARENA` type descriptor with 6 fields (backing_allocator, curr_block, total_used, total_capacity, minimum_block_size, temp_count)
  - Created `stubs/core/mem/virtual.odin` with Arena infrastructure (arena_init, arena_alloc, arena_free_all, arena_destroy, arena_allocator_proc)
  - Created `stubs/core/runtime/default_temporary_allocator.odin` with Default_Temp_Allocator
  - Added `free_all(allocator: Allocator)` builtin that calls allocator's `.Free_All` mode
  - IR intrinsic `ir_gen_intrinsic_free_all` for runtime implementation
  - Context entry point initializes temp_allocator with default_temp_allocator_proc and global arena
- [ ] `builder_make(n, allocator)` — needs `make` builtin to support allocator parameter (not yet implemented)
- [ ] `builder_make_temp(n)` — needs `make` to support allocator parameter
- [ ] `tprint`, `tprintln`, `tprintf`, `tprintfln` (temp allocator) — blocked by `make` allocator parameter support
- [ ] `bprint`, `bprintfln`, `bprintf`, `bprintfln` (buffer-based)
- [ ] `caprint`, `caprintfln`, `caprintf`, `caprintfln` (C string)
- [ ] `wprint`, `wprintln`, `wprintf`, `wprintfln` (io.Writer) — needs io.Writer abstraction

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

## Estimated Effort
- printf/printfln refactoring: 2-3 hours
- Test additions for new specifiers in printf: 1 day
- String truncation (`%.5s`), `%q`, `%m`: 1 day
- Positional/named arguments: 2-3 days (AST grammar changes)
- Builder variants (temp allocator done): 1-2 days (blocked by temp allocator / io.Writer)
- Complex type formatting: 2-3 days
- Custom formatters: 1-2 days

Total rest: 6-10 days for complete implementation.
