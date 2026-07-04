# Unsupported Language Features

Features present in the official Odin language that our compiler does not yet support. These are tracked here for future implementation reference.

## Compile-Time / Polymorphic

- `$T` / `$N` — Compile-time polymorphic parameters (generic procedures)
- Proc overload bundles — `proc{fn1, fn2}` syntax
- `#type` — Procedure type alias syntax

## Built-in Procedures

- `type_info_of(T)` — Get runtime type info from a typeid
- `typeid_of(T)` — Get typeid from a type
- `swizzle` — Vector swizzle operation
- `expand_values` / `compress_values` — Struct/array expansion
- `soa_zip` / `soa_unzip` — SOA struct manipulation

## Annotations / Attributes

- `@(builtin)` — Builtin annotation
- `@(require_results)` — Require results annotation
- `@(link_name="...")` — Custom link name
- `@(private)` — Visibility control
- `#no_bounds_check` — Disable bounds checking
- `#caller_location` — Built-in caller location parameter
- `#assert[expr]` — Compile-time assertion (has basic support, may be incomplete)

## Types

- `Maybe(T)` — Optional type
- `[^]T` — Pointer-to-array with `fmt:` tags

## Control Flow

- `#partial switch` — Partial switch (no exhaustiveness check)
- `when` with complex enum comparisons (e.g., `when ODIN_OS == .Windows`)

## Calling Conventions

- `"contextless"` — Contextless procedure calling convention
- `"odin"` — Default Odin calling convention (has basic support)

## Advanced Features

- `union` type variants (our union support is basic — no tagged union iteration)
- `transmute` in some contexts
- `#soa[]` SOA slice syntax
- `#align` struct alignment attribute
- `distinct` type creation
- `bit_set` with explicit underlying type
- Cross-module `import "base:intrinsics"` or `import "core:mem"` (collection prefix imports)
- Built-in `print_string(s: string)` — Compiler keyword for raw string output
- `core:fmt` package stub with `println` using `print_string`

## Recently Added (Supported)

- `---` — Bodyless procedure declarations (used for builtins/stubs)
- Standard library stubs via `ODIN_ROOT` resolution + linker integration (clang)
- Error messages now include source location: `<file>:<line>:<col>`
- `size_of(T)` / `align_of(T)` / `offset_of(T, field)` — Compile-time queries
- `raw_data(expr)` — Get raw pointer to backing data
- `min(a, b)` / `max(a, b)` — Generic min/max
- `i128` / `u128` — 128-bit integer types
- `typeid` — Type identifier type
- `f16` — 16-bit float type
- `cstring` — C-compatible null-terminated string pointer
- `uintptr` — Unsigned pointer-sized integer
- Endian-specific types — `i16le`, `i32be`, `f64le`, etc. (on x86_64 LE)
- `complex32` / `complex64` / `complex128` — Complex types
- `quaternion64` / `quaternion128` / `quaternion256` — Quaternion types
- `complex(r, i)` / `quaternion(r, i, j, k)` — Complex/quaternion construction
- `TypeName(expr)` — Auto-cast-by-juxtaposition (e.g. `f32(1.0)`)
- `type_of(expr)` — Compile-time type reflection built-in
- `Maybe(T)` — Optional type with `none` / `or_else` / `.value`
