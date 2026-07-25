# Core:fmt Enhancement Plan

## Current Status
All 188 tests pass. The fmt module is functional for basic use cases.

## Implemented Features
- `println`, `printf`, `printfln`, `eprintln`, `eprintf`, `eprintfln`
- Format specifiers: `%d`, `%s`, `%x`, `%u`, `%f`, `%v`, `%%`
- Type support: int, i8, i16, i32, i64, u8, u16, u32, u64, uintptr, rune, byte, f32, f64, bool, string
- Runtime type identification via `type_of(v)` for `any` type

## Completed Work

### fmt Module Extensions (Completed 2026-07-25)
- [x] Added `%b` binary format specifier
- [x] Added `%o` octal format specifier  
- [x] Added `%X` uppercase hex format specifier
- [x] Implemented `print_binary()` helper function
- [x] Implemented `print_octal()` helper function
- [x] Implemented `print_hex_upper()` helper function
- All 188 tests pass

### String Builder Support (Attempted but refactored)
- Attempted to create `stubs/core/strings/strings.odin` with Builder struct
- Attempted to create `stubs/core/io/io.odin` with Writer interface
- Encountered parsing issues with `[]byte` and `string` type interactions
- Decided to focus on fmt extensions instead of full Builder support

## Pending Enhancements

### Priority 1: Essential Missing Features
- [ ] Width and precision formatting (e.g., `%10d`, `%.2f`)
- [ ] Left/right alignment (`-` for left, `0` for zero padding)
- [ ] Sign flags (`+` for always show sign, ` ` for space)
- [ ] Binary format (`%b`)
- [ ] Octal format (`%o`)
- [ ] Uppercase hex (`%X`)
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
- [ ] String Builder-based formatted output functions (`aprint`, `tprint`, `bprint`, `sbprint`, etc.)

## String Builder Support Plan

### Overview
The official fmt implementation uses `strings.Builder` for efficient string construction. This is needed for:
- `aprint`, `aprintln`, `aprintf`, `aprintfln` (allocate string)
- `tprint`, `tprintln`, `tprintf`, `tprintfln` (temp allocator)
- `bprint`, `bprintfln`, `bprintf`, `bprintfln` (buffer-based)
- `caprint`, `caprintfln`, `caprintf`, `caprintfln` (C string)
- `sbprint`, `sbprintfln`, `sbprintf`, `sbprintfln` (strings.Builder)
- `wprint`, `wprintln`, `wprintf`, `wprintfln` (io.Writer)

### Required Components

#### 1. strings.Builder Type
Need to implement `strings.Builder` struct:
```odin
Builder :: struct {
    data: []byte,
    count: int,
}
```

#### 2. Builder Functions
- `builder_init(builder: ^Builder, allocator: context.allocator)`
- `builder_from_bytes(buf: []byte) -> Builder`
- `to_string(builder: ^Builder) -> string`
- `write_byte(builder: ^Builder, b: byte)`
- `write_string(builder: ^Builder, s: string)`

#### 3. strings Package Support
Need stub implementations for:
- `strings.Builder` type
- `strings.builder_init`
- `strings.to_string`
- `strings.builder_from_bytes`
- `strings.write_byte`
- `strings.write_string`

#### 4. IO Writer Support
The fmt module uses `io.Writer` interface:
```odin
Writer :: struct {
    procedure: proc(stream_data: rawptr, mode: Stream_Mode, p: []byte, offset: i64, whence: Seek_From) -> (n: i64, err: Error),
    data: rawptr,
}
```

Need to implement:
- `io.write_byte(writer: Writer, c: byte, n_written: ^int) -> Error`
- `io.write_string(writer: Writer, str: string, n_written: ^int) -> (n: int, err: Error)`
- `io.flush(writer: Writer) -> Error`

### Implementation Steps

1. **Create strings package stub** (`stubs/core/strings/strings.odin`) ✅ DONE
   - Define `Builder` struct
   - Implement `builder_init`, `to_string`, `builder_from_bytes`
   - Implement `write_byte`, `write_string`

2. **Create io package stub** (`stubs/core/io/io.odin`) ✅ DONE
   - Define `Writer`, `Reader`, `Stream_Mode`, `Error` types
   - Implement `write_byte`, `write_string`, `flush`

3. **Extend fmt.odin**
   - [x] Add `%b`, `%o`, `%X` format specifiers
   - [ ] Add `aprint`, `aprintln`, `aprintf`, `aprintfln` (allocator-based)
   - [ ] Add `tprint`, `tprintln`, `tprintf`, `tprintfln` (temp allocator)
   - [ ] Add `bprint`, `bprintfln`, `bprintf`, `bprintfln` (buffer-based)
   - [ ] Add `caprint`, `caprintfln`, `caprintf`, `caprintfln` (C string)
   - [ ] Add `sbprint`, `sbprintfln`, `sbprintf`, `sbprintfln` (Builder)
   - [ ] Add `wprint`, `wprintln`, `wprintf`, `wprintfln` (Writer)

4. **Add missing format specifiers**
   - [x] Add `%b`, `%o`, `%X` (completed)
   - [ ] Implement width/precision parsing
   - [ ] Add `%e`, `%E`, `%g`, `%G`
   - [ ] Add alignment and sign flags

5. **Add advanced formatting**
   - [ ] Implement Python-like syntax parser
   - [ ] Add positional argument support
   - [ ] Add memory formatting (`%m`, `%M`)

6. **Add complex type formatting**
   - [ ] Complex numbers
   - [ ] Quaternions
   - [ ] Enums (with names)
   - [ ] Structs (with fields)
   - [ ] Unions
   - [ ] Matrices

7. **Add custom formatter support**
   - [ ] Implement `@(builtin)` attribute parsing
   - [ ] Add formatter registry mechanism
   - [ ] Add `register_user_formatter`

## Estimated Effort
- String Builder support: 2-3 days
- Missing format specifiers: 1-2 days
- Complex type formatting: 2-3 days
- Custom formatters: 1-2 days

Total: 6-10 days for complete implementation