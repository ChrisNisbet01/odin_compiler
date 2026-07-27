# Core:fmt Enhancement Plan

## Current Status
All 205 tests pass. The fmt module supports basic printing, full format specifier handling with flags/width/precision (via `sb_format_parsed`), strings.Builder-based output, cross-package imports, and `..any` variadic forwarding.

## Compiler Bugs Fixed During This Work

### Fixed in this session (2026-07-28)

- **Context initializer stored allocator global address instead of value**: Entry point stored `ptr @default_allocator` (address of global variable) as allocator procedure pointer instead of the global's VALUE `{ @__odin_default_alloc, null }`. Calling the address crashed with SIGSEGV. Fixed by using `LLVMGetInitializer(default_alloc_global)`.
- **`int_to_string` returned dangling pointer**: Used stack `alloca [21 x i8]` for the digit buffer. Callers like `sb_print_string` → `append` clobbered the dead stack memory before all bytes were read. Fixed by heap-allocating via the context allocator.
- **`make([dynamic]T, len, cap)` support for pre-allocated capacity**: `strings.builder_make(n)` called `make([dynamic]byte, n)` which created len=n, cap=n. `append` wrote at position n, but `to_string` read positions 0..count (all zeros). Fixed by treating `children[2]` as cap for DAs/slices. Updated `strings.odin` to use `make([dynamic]byte, 0, n)`.
- **String `==`/`!=` comparison**: `icmp eq` on `{ptr, i64}` struct operands is invalid LLVM IR. Fixed by adding `ir_gen_string_compare` in `ir_gen_operator.c` — compares lengths first (fast-fail via `icmp eq`), then calls `memcmp` on data pointers if lengths match. For `!=`, the `==` result is negated.

### Pre-existing Limitations (Still Open)

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
- `aprint`, `aprintln` — return allocated string
- `aprintf`, `aprintfln` — formatted versions that forward `..any` to `sb_format_parsed`

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
- `builder_make(n)` now uses `make([dynamic]byte, 0, n)` for zero-length pre-allocated capacity
- `grow` also uses `make([dynamic]byte, 0, new_cap)` correctly

## Pending Refactoring Work

### ~~HIGH: Migrate `printf`/`printfln`/`eprintf`/`eprintfln`/`sbprintf`/`sbprintfln` to `sb_format_parsed`~~
**Status**: DONE — all format-style entry points already delegate to `sb_format_parsed`.

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
- [x] `builder_make(n, allocator)` — `make` now supports allocator parameter (grammar allows 3 args).
- [x] `builder_make_temp(n)` — works via `builder_make(n, context.temp_allocator)`.
- [x] `tprint`, `tprintln`, `tprintf`, `tprintfln` (temp allocator) — all working and tested.
- [ ] `bprint`, `bprintfln`, `bprintf`, `bprintfln` (buffer-based).
- [ ] `caprint`, `caprintfln`, `caprintf`, `caprintfln` (C string).
- [ ] `wprint`, `wprintln`, `wprintf`, `wprintfln` (io.Writer) — needs io.Writer abstraction.

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
- Buffer/C string/Writer variants: 2-3 days
- Complex type formatting: 2-3 days
- Custom formatters: 1-2 days

Total rest: 5-9 days for complete implementation.
