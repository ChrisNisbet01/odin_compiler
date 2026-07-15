# Unsupported Language Features

Features present in the official Odin language that our compiler does not yet support, ordered by estimated implementation complexity (easiest first).

## Recently fixed
- **`type_info_of(T)` — Runtime type info**: Returns a `^type_info` pointer with `size`, `align`, `id`, `kind` fields. LLVM globals generated lazily per unique type and deduplicated by `type_id`. Works with all basic and derived types.
- **`#assert[expr]` compile-time assertions**: Now fully functional. Enforces exactly one expression. Evaluates at compile-time in both procedure bodies and top-level scope. Fails with `#assert failed` error when expression evaluates to false. Works with arithmetic, boolean, comparison, bitwise, `typeid_of`, `size_of`, `align_of`, and `offset_of` expressions.
- **`#simd [N]T` vector types**: Added `KwSimd` lexeme (`#simd` with IdBoundary), `VectorType` grammar rule. AST node `AST_NODE_VECTOR_TYPE`, type descriptor `TD_KIND_VECTOR` with `lane_count`, `element_type`, LLVM vector type. Semantic analyser resolves element type and lane count, creates `get_or_create_vector_type`. Swizzle: validates field name against {x,y,z,w,r,g,b,a} sets (no mixing), resolves single-component to element type, multi-component to new vector type. IR generator: rvalue swizzle uses `ExtractElement` (single) or `ShuffleVector` (multi). Rvalue subscript uses `ExtractElement`. Lvalue subscript (`v[idx] = val`) uses load + `InsertElement` + store pattern. Lvalue swizzle (`v.x = val`, `v.xy = val`, `v.xyzw = val`) uses ExtractElement + InsertElement per-lane merge pattern. Compound assignment (`+=`, `-=`) supported for both single and multi-component swizzle.
- **`core:os` + runtime intrinsics**: `os.exit()` now uses runtime intrinsic `os_exit` (inline syscall, no `foreign libc`). `print_string`/`print_byte`/`int_to_string` refactored from special AST nodes to `core:runtime` prelude auto-import + IR generator intrinsic body generation. `stubs/src/` deleted (dead code).
- **Recursive function calls**: Fixed link error (`fib.4` undefined) by moving `generator_add_symbol` before body generation in IR generator. Fixed recursive call semantic analysis (returned NULL type for recursive calls) by pre-registering procedure type in symbol table before body analysis. `fibonacci.odin` now compiles without any casts.
- **Implicit conversion of untyped literals**: Added `sem_can_implicitly_convert()` — recognizes `AST_NODE_INTEGER_VALUE`/`AST_NODE_FLOAT_VALUE` as untyped literals that can convert to any matching numeric type. Applied to return statement type checks. IR generator coerces return values to match function return type.
- **`printf %d/%x` with unsigned types**: Delegated `%d` and `%x` to `print_value` in stubs `fmt.odin` (handles all integer types uniformly).
- **Extended `core:fmt` variants**: Added `printfln`, `eprintln`, `eprintf`, `eprintfln` to `stubs/core/fmt/fmt.odin`. Each is a standalone copy (no `..args` forwarding, no `[]any` param delegation — both unsupported). Tested via `test_fmt_more.odin`.

## Low Complexity

(None remaining)

## Medium Complexity

(None remaining)

## Medium Complexity (Completed)

- **`[^]T` — Multi-pointer type**: Grammar `MultiPointerType = [^] TypePrefix`. AST node `AST_NODE_MULTI_POINTER_TYPE`, type descriptor `TD_KIND_MULTI_POINTER` (LLVM pointer type, canonical name `[^]%s`). Semantic analysis resolves element type, creates type descriptor, subscript resolves to element type. IR generator: lvalue subscript loads pointer then GEP; rvalue subscript GEP then auto-loaded for non-composite types; member-access auto-dereference; explicit deref `^`. Works with basic types and structs (e.g. `[^]int`, `[^]Point`). Type info kind = 17.
- **`#type` — Procedure type alias syntax**: Grammar adds optional `KwHashType` prefix to `ProcedureSignature`. No new AST node — reuses `AST_NODE_PROCEDURE_SIGNATURE`. `#type proc(x: int) -> int` is semantically identical to `proc(x: int) -> int`.

## Very High Complexity

### Vector swizzle lvalue ✅ IMPLEMENTED
Single-component (`v.x = val`) and multi-component (`v.xy = val`, `v.xyzw = val`, etc.) swizzle lvalue assignment now work. Uses `ExtractElement`/`InsertElement` per-lane merge pattern (no `ShuffleVector` to avoid LLVM mismatched-length operand restriction). Compound assignment (`+=`, `-=`) supported for both forms.

### `expand_values` / `compress_values` ✅ IMPLEMENTED

### `soa_zip` / `soa_unzip`
SOA struct field manipulation. Requires multi-field extract/insert at the IR level.

### `$T` / `$N` — Compile-time polymorphic parameters (generics)
Generic procedures with polymorphic type/value parameters. Requires major type system overhaul, monomorphization, and full compile-time evaluation. The largest missing feature.

## Bugs

- **Pointer/integer comparison silently accepted**: The semantic analyser accepts `^int != int` (pointer compared directly with an integer literal) without error. The IR generator compiles this to `icmp ne ptr, inttoptr(i64 X to ptr)` — comparing a stack address as if it were an integer value. The official Odin language rejects this as a type mismatch. Discovered while writing `test_bounds_check_short_circuit.odin` — the test works around it by using `n4^ != 7` (explicit dereference to compare the pointed-to integer). Location: semantic-analyser-side comparison checks (`sem_types_assignable`/`sem_check_assignment` and the binary-expression comparison path).

## Earlier Completions

## Bugs Fixed

- **Bounds-check PHI predecessor bug** (session 2026-07-16): `ir_gen_emit_bounds_check` splits basic blocks without updating downstream PHI nodes. When a bounds-checked subscript appeared inside a `||`/`&&` short-circuit chain, the merge-block PHI referenced the original predecessor block (now mid-chain) instead of the continuation block (the actual new predecessor). This caused an LLVM FastISel crash during code generation. **Fix**: three PHI-construction sites in `ir_gen_logical_short_circuit` and `ir_gen_or_else` (two variants) now re-capture `LLVMGetInsertBlock(ctx->builder)` AFTER RHS evaluation and use that as the PHI's incoming block (mirroring the existing correct pattern in the ternary handler). Test: `tests/test_bounds_check_short_circuit.odin` (32 subtests covering all three sites). No regressions.

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
- **Bounds checking on array/slice/string subscripts**: `llvm.trap()` emitted for out-of-bounds access. Index compared against length (compile-time for arrays, loaded `.len` field for slices/strings). `#no_bounds_check` directive suppresses the checks.
- `#no_bounds_check` — Functions as intended: disables bounds checking for subsequent code in the same scope.
- `#partial switch` — Functions as intended: suppresses exhaustiveness checking for enum-typed switches.
- **Exhaustiveness checking for enum switches**: Switch statements on enum types are checked for completeness. Missing enumerator cases produce a `switch is not exhaustive: missing case for enum value '<name>'` error. The `#partial` directive (placed after `switch` keyword, e.g. `switch #partial c`) suppresses the check. A `default` case also suppresses the check. Enumerators are stored in the `TD_KIND_ENUM` type descriptor (`enumerator_names`, `enumerator_values`, `enumerator_count` fields) by the semantic analyser during `AST_NODE_ENUM_TYPE` processing.
- Chained member access with reserved keyword field names (`.len`, `.data`, `.cap` on string/slice/dynamic_array/array/maybe; pointer auto-dereference `p.field` in rvalue context)
- Slice expression syntax (`arr[low..high]`, `arr[..]`, `arr[low..]`, `arr[..high]`, `arr[..<high]`, `arr[low..<high]`) with semantic analysis and IR generation (GEP + slice struct construction); works for both arrays and slices, including chained slicing
- Type alias `::` declaration (`Handle :: int`, `PtrToInt :: ^int`, `MyHandle :: Handle`): Added `TypePrefix` alternative to `ConstantDecl` grammar. Semantic analyser detects type values and stores them as `SYMBOL_TYPE` symbols; `sem_resolve_type_expr` handles `AST_NODE_IDENTIFIER` for type alias lookups. Variable declarations (`x: Handle`) resolve type via the identifier scope lookup path.
- `#align` struct alignment (`struct #align N { ... }` and field-level `field: type #align N`): Grammar parses `KwAlign` directive + size on both struct type and struct fields. Semantic analyser extracts alignment from directive children, overrides `struct_metadata.alignment` after type registration. IR generator uses user alignment in `LLVMSetAlignment` for struct allocas.
- **`delimited_flex` fix**: Changed all `delimited(X, Sep)` to `delimited_flex(X, Sep)` in grammar rules with `Sep?` trailing optional. The strict `delimited` combinator errors on trailing separators, making the `Sep?` dead code. `delimited_flex` backtracks over trailing separators, fixing `:: struct { x: int; }` and similar patterns across struct/union field lists, attribute lists, enumerator lists, return lists, and identifier lists.
- **`@(builtin)` — Builtin annotation**: Added `bool is_builtin` to `ProcDeclAttributes`. `sem_analyse_attributes()` parses `@(builtin)` and stores it. `ir_gen_top_level_decl()` uses `is_builtin` as a primary signal for intrinsic body generation (alongside name-based fallback for backward compatibility). Unknown builtins emit a compile-time error. All runtime intrinsics in `stubs/core/runtime/runtime.odin` now use `@(builtin)`.
- **`proc{fn1, fn2}` — Overload bundles**: Complete implementation across all compiler layers. Grammar: `ProcOverloadBundle = KwProc LBrace Identifier (Comma Identifier)* RBrace` with `@AST_ACTION_PROC_OVERLOAD_BUNDLE` action. Type descriptors: `TD_KIND_OVERLOAD_BUNDLE` with `candidate_types`/`candidate_symbols` arrays, deduplicated by pointer-equality of candidate types, no LLVM type (`llvm_type = NULL`). Semantic analyser (pass 1): registers bundle symbol in scope, resolves candidate symbols and types, creates bundle type. Semantic analyser (pass 2): re-resolves candidates, updates `resolved_type`. Semantic analyser (call resolution): `sem_resolve_overload_bundle_call()` helper evaluates arg expressions, matches via `sem_types_assignable`, selects unique best match, emits "no matching overload" or "ambiguous call" errors. IR generator: POSTFIX_CALL reads `op->resolved_symbol` (set by semantic analyser), forward-declares winning function if needed. Works with both package-qualified (`pkg.foo(args)`) and normal (`foo(args)`) calls. Tested with 3 test files (basic dispatch, no-match, ambiguous).

## Earlier Completions (Grammar Fixes)

- **`if cond do stmt`**: Added `KwDo` lexeme (reserved as keyword). `IfStatement` grammar accepts `CompoundStatement | (KwDo Statement)` for both then and else branches. Semantic analyser dispatches `Statement` children to `sem_pass2_node` (not `sem_evaluate_expr`) so statement types like `os.exit()` are correctly analysed. Works with `else if`, `else do`, and mixed `{ }`/`do` forms.
- **`arr[:]` full-slice syntax**: Added `PostfixOpFullSlice` grammar rule (`LBracket Colon RBracket`) reusing `AST_ACTION_POSTFIX_SLICE`. Semantically and at IR level, `arr[:]` is identical to `arr[..]` — produces a slice covering the full array.
- **Compound type `is_type_node()` identifier fix**: All compound type resolvers (`[5]T`, `[]T`, `[^]T`, `[dynamic]T`, `distinct T`, `map[T]U`) used `is_type_node()` to find the element type child, but `is_type_node()` returns `false` for `AST_NODE_IDENTIFIER`. Broke types when the element was a struct name or type alias (e.g. `[5]Point`, `[]MyType`, `[^]Handle`). Fixed by adding `|| child->type == AST_NODE_IDENTIFIER` in all 6 handlers.
- **Multi-pointer subscript lvalue loading**: `p[0]` on `[^]T` produced a GEP pointer; rvalue uses like `v: Point = p[0]` stored the pointer instead of the struct value. Fixed by adding pointer→value auto-load in `ir_gen_variable_decl` when init value is a pointer but resolved type is a non-pointer value type.
