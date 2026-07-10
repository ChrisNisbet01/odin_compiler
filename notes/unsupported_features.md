# Unsupported Language Features

Features present in the official Odin language that our compiler does not yet support, ordered by estimated implementation complexity (easiest first).

## Recently fixed
- **`core:os` + runtime intrinsics**: `os.exit()` now uses runtime intrinsic `os_exit` (inline syscall, no `foreign libc`). `print_string`/`print_byte`/`int_to_string` refactored from special AST nodes to `core:runtime` prelude auto-import + IR generator intrinsic body generation. `stubs/src/` deleted (dead code).
- **Recursive function calls**: Fixed link error (`fib.4` undefined) by moving `generator_add_symbol` before body generation in IR generator. Fixed recursive call semantic analysis (returned NULL type for recursive calls) by pre-registering procedure type in symbol table before body analysis. `fibonacci.odin` now compiles without any casts.
- **Implicit conversion of untyped literals**: Added `sem_can_implicitly_convert()` — recognizes `AST_NODE_INTEGER_VALUE`/`AST_NODE_FLOAT_VALUE` as untyped literals that can convert to any matching numeric type. Applied to return statement type checks. IR generator coerces return values to match function return type.
- **`printf %d/%x` with unsigned types**: Delegated `%d` and `%x` to `print_value` in stubs `fmt.odin` (handles all integer types uniformly).
- **Extended `core:fmt` variants**: Added `printfln`, `eprintln`, `eprintf`, `eprintfln` to `stubs/core/fmt/fmt.odin`. Each is a standalone copy (no `..args` forwarding, no `[]any` param delegation — both unsupported). Tested via `test_fmt_more.odin`.

## Low Complexity

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

### `odinc run` command line support
compile and run an odin file directly from the odin compiler command line.

### `type_info_of(T)` — Get runtime type info
Returns a pointer to the type descriptor at runtime. Requires generating type info data sections in LLVM IR.

### `distinct` type creation
Parsed but semantically transparent (doesn't create a new type). Need `get_or_create_distinct_type` that allocates a separate descriptor, and enforce type distinctness in assignment/equality.

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
- `@(link_name="...")` — Custom link name (rename symbols at link time)
- `@(require_results)` — Require results annotation
- `@(private)` — Visibility control (cross-package access restriction)
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
- `core:os` package with `os.exit()` (runtime intrinsic via inline syscall)
- Void-only `main :: proc()` (exit code via `os.exit()`; semantic error for non-void main)
- Runtime intrinsics via `core:runtime` auto-import (print_string, print_byte, int_to_string, os_exit)
- Inline syscall IR generation for runtime intrinsics (SYS_write, SYS_exit)
- `"contextless"` calling convention (IR gen correctly skips context parameter prepend/inject)
