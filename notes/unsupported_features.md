# Unsupported Language Features

Features present in the official Odin language that our compiler does not yet support, ordered by estimated implementation complexity (easiest first).

## Recently fixed
- **`type_info_of(T)` — Runtime type info**: Returns a `^type_info` pointer with `size`, `align`, `id`, `kind` fields. LLVM globals generated lazily per unique type and deduplicated by `type_id`. Works with all basic and derived types.
- **`#assert[expr]` compile-time assertions**: Now fully functional. Enforces exactly one expression. Evaluates at compile-time in both procedure bodies and top-level scope. Fails with `#assert failed` error when expression evaluates to false. Works with arithmetic, boolean, comparison, bitwise, `typeid_of`, `size_of`, `align_of`, and `offset_of` expressions.
- **`core:os` + runtime intrinsics**: `os.exit()` now uses runtime intrinsic `os_exit` (inline syscall, no `foreign libc`). `print_string`/`print_byte`/`int_to_string` refactored from special AST nodes to `core:runtime` prelude auto-import + IR generator intrinsic body generation. `stubs/src/` deleted (dead code).
- **Recursive function calls**: Fixed link error (`fib.4` undefined) by moving `generator_add_symbol` before body generation in IR generator. Fixed recursive call semantic analysis (returned NULL type for recursive calls) by pre-registering procedure type in symbol table before body analysis. `fibonacci.odin` now compiles without any casts.
- **Implicit conversion of untyped literals**: Added `sem_can_implicitly_convert()` — recognizes `AST_NODE_INTEGER_VALUE`/`AST_NODE_FLOAT_VALUE` as untyped literals that can convert to any matching numeric type. Applied to return statement type checks. IR generator coerces return values to match function return type.
- **`printf %d/%x` with unsigned types**: Delegated `%d` and `%x` to `print_value` in stubs `fmt.odin` (handles all integer types uniformly).
- **Extended `core:fmt` variants**: Added `printfln`, `eprintln`, `eprintf`, `eprintfln` to `stubs/core/fmt/fmt.odin`. Each is a standalone copy (no `..args` forwarding, no `[]any` param delegation — both unsupported). Tested via `test_fmt_more.odin`.

## Medium Complexity

### `@(builtin)` — Builtin annotation
Marks a procedure as a compiler intrinsic. Would integrate with the built-in proc dispatch system.

### Bounds checking on array/slice/map subscripts
Currently subscript operations (`x[i]`) generate no runtime bounds checks. Need to emit index-vs-length comparisons and trap/branch on out-of-bounds. The `#no_bounds_check` directive should then suppress these checks.

### Exhaustiveness checking for switches
Currently switch statements accept any set of cases without verifying completeness. Need to detect when a switch (without `#partial`) covers all variants of an enum or other finite type, and emit an error for missing cases. The `#partial switch` directive should suppress this check.

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

## Earlier Completions

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
- `distinct` type creation with type isolation (`MyInt :: distinct int`): creates a unique type descriptor; enforces assignment only with explicit cast; untyped literals implicitly convert to distinct numeric types
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
- `odinc run` command (compile, link, and execute in one step)
- Octal literal syntax (`0o644`, `0o777`, etc.)
- Bitwise OR constant folding (`os.O_WRONLY | os.O_CREAT | os.O_TRUNC`) — compile-time evaluation of named constants in `when` conditions and constant declarations
- `#caller_location` — returns `Source_Location` struct (`file`, `line`, `column`) with the source location of the expression
- `#no_bounds_check` — Grammar accepts it; no-op at IR level (bounds checking not yet implemented, so the directive is correct as a no-op marker)
- `#partial switch` — Grammar accepts it; no-op at semantic level (exhaustiveness checking not yet implemented, so it's correct as a no-op marker)
- Chained member access with reserved keyword field names (`.len`, `.data`, `.cap` on string/slice/dynamic_array/array/maybe; pointer auto-dereference `p.field` in rvalue context)
- Slice expression syntax (`arr[low..high]`, `arr[..]`, `arr[low..]`, `arr[..high]`, `arr[..<high]`, `arr[low..<high]`) with semantic analysis and IR generation (GEP + slice struct construction); works for both arrays and slices, including chained slicing
- Type alias `::` declaration (`Handle :: int`, `PtrToInt :: ^int`, `MyHandle :: Handle`): Added `TypePrefix` alternative to `ConstantDecl` grammar. Semantic analyser detects type values and stores them as `SYMBOL_TYPE` symbols; `sem_resolve_type_expr` handles `AST_NODE_IDENTIFIER` for type alias lookups. Variable declarations (`x: Handle`) resolve type via the identifier scope lookup path.
- `#align` struct alignment (`struct #align N { ... }` and field-level `field: type #align N`): Grammar parses `KwAlign` directive + size on both struct type and struct fields. Semantic analyser extracts alignment from directive children, overrides `struct_metadata.alignment` after type registration. IR generator uses user alignment in `LLVMSetAlignment` for struct allocas.
- **`delimited_flex` fix**: Changed all `delimited(X, Sep)` to `delimited_flex(X, Sep)` in grammar rules with `Sep?` trailing optional. The strict `delimited` combinator errors on trailing separators, making the `Sep?` dead code. `delimited_flex` backtracks over trailing separators, fixing `:: struct { x: int; }` and similar patterns across struct/union field lists, attribute lists, enumerator lists, return lists, and identifier lists.
