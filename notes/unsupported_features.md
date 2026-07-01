# Unsupported Language Features

Features present in the official Odin standard library that our compiler does not yet support. These are tracked here for future implementation reference.

## Compile-Time / Polymorphic

- `$T` / `$N` — Compile-time polymorphic parameters (generic procedures)
- `type_of(expr)` — Compile-time type reflection built-in
- Proc overload bundles — `proc{fn1, fn2}` syntax
- `---` — Bodyless procedure declarations (used for builtins)
- `#type` — Procedure type alias syntax

## Built-in Procedures

- `type_info_of(T)` — Get runtime type info from a typeid
- `typeid_of(T)` — Get typeid from a type
- `size_of(T)` / `align_of(T)` / `offset_of(T, field)` — Compile-time queries
- `swizzle` — Vector swizzle operation
- `complex` / `quaternion` — Complex/quaternion construction
- `expand_values` / `compress_values` — Struct/array expansion
- `raw_data` — Get raw pointer to backing data
- `soa_zip` / `soa_unzip` — SOA struct manipulation
- `min` / `max` — Generic min/max

## Annotations / Attributes

- `@(builtin)` — Builtin annotation
- `@(require_results)` — Require results annotation
- `@(link_name="...")` — Custom link name
- `@(private)` — Visibility control
- `#no_bounds_check` — Disable bounds checking
- `#caller_location` — Built-in caller location parameter
- `#assert[expr]` — Compile-time assertion (has basic support, may be incomplete)

## Types

- `i128` / `u128` — 128-bit integer types
- `f16` — 16-bit float
- `complex32` / `complex64` / `complex128` — Complex types
- `quaternion64` / `quaternion128` / `quaternion256` — Quaternion types
- `cstring` — C-compatible null-terminated string pointer
- `typeid` — Type identifier type
- `uintptr` — Unsigned pointer-sized integer
- Endian-specific types — `i16le`, `i32be`, `f64le`, etc.
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
- Cross-module `import "base:intrinsics"` or `import "core:mem"` (no `:` collection prefix support)
