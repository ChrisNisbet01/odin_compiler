# Unsupported Language Features

Features present in the official Odin language that our compiler does not yet support, ordered by estimated implementation complexity (easiest first).

## Low Complexity

### `@(link_name="...")` — Custom link name
Use to rename symbols at link time for FFI. Grammar needs `@(...)` attribute syntax on declarations; IR generator uses the string as the LLVM symbol name.

### `@(require_results)` — Require results annotation
Marks a procedure so callers who ignore its return value get a warning. Add flag to proc descriptor; check at call sites.

### `@(private)` — Visibility control
Restricts symbol access to the declaring package. Add flag to `PackageScope` entries; check during cross-package lookup.

### `typeid_of(T)` — Get typeid from a type ✅ DONE
Returns the hash-based typeid of a compile-time-known type. Very similar to `type_of(T)` — uses the hash-based type ID system.

### `#caller_location` — Built-in caller location parameter
Injects source location (`file:line:col`) as an implicit proc parameter. Similar mechanism to context threading.

### `#assert[expr]` compile-time assertions
Basic support exists (evaluates condition, errors on false). May be incomplete for complex expressions.

### `when` with complex enum comparisons
`when ODIN_OS == .Windows` requires compile-time enum resolution in `when` conditions. Currently only `when true`/`when false` works reliably.

### `#no_bounds_check` — Disable bounds checking
Attribute to skip bounds checks on array/slice subscript. Grammar change for `#` directives; IR gen skips the cmp+trap.

### `#partial switch` — Partial exhaustiveness
Switch without the default exhaustiveness check. Simple grammar modifier.

## Medium Complexity

### `"contextless"` calling convention
Parsed and used in stubs, but IR gen doesn't correctly suppress the context parameter. Fix: skip context alloca prepend for `contextless` calls.

### `type_info_of(T)` — Get runtime type info
Returns a pointer to the type descriptor at runtime. Requires generating type info data sections in LLVM IR.

### `distinct` type creation
Parsed but semantically transparent (doesn't create a new type). Need `get_or_create_distinct_type` that allocates a separate descriptor, and enforce type distinctness in assignment/equality.

### `bit_set` with explicit underlying type
Grammar accepts `bit_set[u32]` but semantic analyser only implements the range-based path. Add backing-type resolution to `get_or_create_bit_set_type`.

### `#soa[]` SOA slice syntax (no brackets / no size)
`struct #soa { ... }` works (slice-backed SOA), but the bare `#[soa]` directive without `[N]` doesn't parse. Grammar needs `#soa` as a standalone directive.

### `#align` struct alignment
Used to override struct/field alignment. Need to parse the alignment value and pass it to LLVM's struct layout.

### `@(builtin)` — Builtin annotation
Marks a procedure as a compiler intrinsic. Would integrate with the built-in proc dispatch system.

## High Complexity

### `[^]T` — Pointer-to-array with `fmt:` tags
Fat pointer carrying array length/stride info. New type kind with grammar, type system, and IR changes.

### `#type` — Procedure type alias syntax
Alternative syntax for procedure type literals. Grammar-only change but interacts with type resolution.

## Very High Complexity

### Proc overload bundles — `proc{fn1, fn2}`
Groups multiple procedures under one name for overload resolution by parameter types. Requires complex dispatch logic.

### `swizzle` — Vector swizzle operation
`v.xyzw` syntax for vector component access. Depends on vector type support.

### `expand_values` / `compress_values`
Struct/array field expansion for variadic functions. Complex IR generation.

### `soa_zip` / `soa_unzip`
SOA struct field manipulation. Requires multi-field extract/insert at the IR level.

### `$T` / `$N` — Compile-time polymorphic parameters (generics)
Generic procedures with polymorphic type/value parameters. Requires major type system overhaul, monomorphization, and full compile-time evaluation. The largest missing feature.

## Recently Added (Supported)

The following features were previously listed as unsupported but are now implemented:

- `---` (bodyless procedure declarations)
- Error messages with source locations (`<file>:<line>:<col>`)
- `size_of(T)` / `align_of(T)` / `offset_of(T, field)`
- `raw_data(expr)`
- `min(a, b)` / `max(a, b)`
- `i128` / `u128`
- `typeid` / `type_of(expr)`
- `type_of(T)` with type-name operands (e.g. `type_of(int)`)
- Stable hash-based type IDs (SipHash-2-4)
- `f16` / `cstring` / `uintptr`
- Endian-specific types (`i16le`, `i32be`, `f64le`, etc.)
- `complex32/64/128`, `quaternion64/128/256`, complex/quaternion constructors
- `TypeName(expr)` auto-cast-by-juxtaposition
- `Maybe(T)` with `none` / `or_else` / `.value`
- Tagged `union` with `.variant` syntax and type assertions
- `transmute` (always bitcast)
- `distinct` (parsed — still transparent, see Medium Complexity above)
- `bit_set` range syntax (e.g. `bit_set[0..<32]`)
- `struct #soa { ... }` (slice-backed SOA struct)
- `#soa[N]` (array-backed SOA)
- Cross-module collection prefix imports (`core:fmt`)
- `print_string(s)` / `print_byte(b)` / `int_to_string(n)` built-ins
- `core:fmt` package with `println`, `printf` (`%d`, `%s`, `%x`, `%v`, `%u`, `%%`)
- Escape sequences in string literals: `\a`, `\b`, `\e`, `\f`, `\n`, `\r`, `\t`, `\v`, `\\`, `\'`, `\"`, `\0`, `\xNN`
- `..any` variadic parameters with call-site packing
- `"odin"` calling convention with context threading
- Entry point wrapper (C-compatible `main()` → `__odin_main`)
- Implicit zero-initialization of local variables
- `for i in expr`, `for i, val in expr` (for-range loops)
- `when` declarations (compile-time conditional compilation)
- `fallthrough` statement
- `defer` statement (LIFO order on scope exit)
- Logical `&&`/`||` short-circuit evaluation
