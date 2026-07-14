## Accomplishments (session 2026-07-13)

### Implemented `distinct` type creation with type isolation
- **`create_distinct_type()`** (`type_descriptors.c`/`.h`): Allocates a new `TD_KIND_DISTINCT` descriptor with same LLVM layout as base, unique hash (no dedup).
- **`is_integer_kind()` / `is_floating_kind()`**: Unwrap `TD_KIND_DISTINCT` to base before checking, so distinct numeric types accept untyped literals.
- **`sem_types_assignable()` / `sem_check_assignment()`**: Enforce distinct-type assignment rules (only assign to same distinct type, or from untyped literal); all other assignments use old lax behaviour.
- **Fixed `AST_NODE_VARIABLE_DECL`**: Added `child->type == AST_NODE_IDENTIFIER` to type-node classification so `x: MyInt` and `x: Handle` (type alias) both resolve correctly.
- **Tests**: `test_distinct.odin` (6 subtests), `test_distinct_assign_base.odin` (expected fail), `test_distinct_to_base.odin` (expected fail). All pass.

### Fixed `i128`/`u128` regression
- **Root cause**: `i128`/`u128` not in `BasicType` grammar rule → parsed as `Identifier` → the new `child->type == AST_NODE_IDENTIFIER` check made them match as type nodes, but `sem_resolve_type_expr` for `AST_NODE_IDENTIFIER` only checked scope (not type registry) → returned NULL → "undeclared identifier".
- **Fix**: Added `KwI128`/`KwU128` lexemes to `BasicType` rule + `AllReservedWords` in grammar; added `get_basic_type_by_name` fallback in `sem_resolve_type_expr` for robustness.
- **All 120 tests pass** (previously 120 passing).

### Implemented `type_info_of(T)` runtime type info
- **Grammar**: Added `KwTypeInfoOf` lexeme, `TypeInfoOfExpr` rule, updated `UnaryExpression` and `AllReservedWords`.
- **AST**: Added `AST_NODE_TYPE_INFO_OF_EXPR` enum, action, node name.
- **Type descriptors**: Added `type_info` struct `{i64 size, i64 align, i64 id, i64 kind}` as a compiler-internal type, registered in `register_builtin_context_types`. Added getter functions for the opaque `TypeDescriptors` struct.
- **Semantic analyser**: `AST_NODE_TYPE_INFO_OF_EXPR` resolves operand type and returns `^type_info` pointer type.
- **IR generator**: `ir_gen_get_or_create_type_info_global()` lazily creates `internal unnamed_addr constant` globals per unique type, deduplicated by `type_id`. `.id` field stores the type's hash. Fixed signed/unsigned comparison bug (`td->type_id >= 0` truncated high-bit-set hashes to 0).
- **Tests**: `test_type_info_of.odin` (19 subtests covering size, id, pointer comparison, kind). **All 121 tests pass**.

## Accomplishments (session 2026-07-12, continued)

### Implemented chained member access with reserved keyword field names
- **Grammar fix**: Added `FieldName = lexeme(identifier) @AST_ACTION_IDENTIFIER` rule. Changed `PostfixOpMember = Dot Identifier` → `PostfixOpMember = Dot FieldName`. This allows reserved keywords (`len`, `cap`, `type`, …) as field names after a dot, since `Identifier` excludes reserved words via `not(AllReservedWords)`.
- **IR gen rvalue POSTFIX_MEMBER**: Added GEP-based field extraction for `string .len` / `.data`, `slice .len` / `.data`, `dynamic_array .len` / `.cap` / `.data`, and `array .len`.
- **IR gen identifier loading**: Added `TD_KIND_BASIC` with name `"string"` to composite types check, so string variables return their alloca pointer (needed for GEP in member access).
- **IR gen auto-load exclusion**: Prevented auto-loading string pointers (the pointer is needed for subsequent GEP).
- **IR gen pointer auto-dereference (rvalue)**: `p.field` now works — `val` is already the pointer value (struct address), just update `cur_type` to pointee without loading.
- **Tests**: `test_chained_member.odin` updated with 11 subtests covering string `.len`/`.data`, array `.len`, `Maybe.value`, chained `loc.file.len`/`loc.file.data`, and pointer deref `p.line`/`p.column`/`p.file.len`.
- **All 115 tests pass** (117 total, 2 pre-existing baseline failures: `test_file_io`, `test_foreign_strings`).

## Accomplishments (session 2026-07-12)

### Implemented `#assert[expr]` compile-time assertions
- **Fixed semantic analyser**: Replaced premature `break` loop in `AST_NODE_DIRECTIVE_WITH_ARGS` handler with proper single-expression enforcement (error on 0 or multiple expressions). The expression is now fully evaluated and checked as a compile-time boolean constant.
- **Added top-level `#assert` support**: Added `AST_NODE_DIRECTIVE_WITH_ARGS` handler in `sem_pass1_register_top_level_ex` so `#assert` works outside procedure bodies too.
- **Updated tests**: Fixed `test_assert_fail.odin` to use void main (so `#assert` failure is the actual reason). Added `test_assert_toplevel.odin` (passing top-level assertions) and `test_assert_toplevel_fail.odin` (expected-to-fail top-level assertion).
- **All 117 tests pass**. Works with arithmetic, boolean, comparison, bitwise, `typeid_of`, `size_of`, `align_of`, and `offset_of` expressions.

## Accomplishments (session 2026-07-11)

### Refactored print_string/print_byte/int_to_string into intrinsic-based runtime package
- **Removed special grammar rules, AST nodes, and lexer tokens** for `print_string`, `print_byte`, `int_to_string`. No more `KwPrintString`, `PrintStringExpr`, etc. The grammar, semantic analyser, and IR generator no longer contain any builtin-specific code for these functions.
- **Created `stubs/core/runtime/runtime.odin`**: New prelude package with `---` bodyless declarations for `print_string(fd: int, str: string)`, `print_byte(fd: int, b: u8)`, `int_to_string(i: int) -> string`.
- **Auto-import mechanism**: `sem_pass1_register_top_level_ex` now auto-imports `core:runtime` as an implicit `using` import for every file. The runtime package is resolved, parsed, analysed, and codegen'd like a normal import.
- **Intrinsic body generation**: `ir_gen_top_level_decl` detects known runtime function names and calls `ir_gen_runtime_intrinsic_body()` to emit LLVM IR bodies (inline syscall for print_string/print_byte; digit loop for int_to_string). This replaces the old special-case AST node handling.
- **Benefits**: The compiler no longer has special-case logic for these 3 functions — they're regular Odin procedures with `---` declarations. Adding new intrinsics is a matter of adding a declaration in `core:runtime` and a case in `ir_gen_runtime_intrinsic_body()`.
- **All 106 tests pass**.

### Converted `os.exit()` from libc call to runtime intrinsic inline syscall
- **Added `os_exit` to `stubs/core/runtime/runtime.odin`**: Bodyless declaration `os_exit :: proc(code: int) ---` alongside print_string/print_byte/int_to_string.
- **Added `os_exit` to intrinsic detection**: `ir_gen_is_runtime_intrinsic()` and `ir_gen_runtime_intrinsic_body()` handle `os_exit` — emits inline syscall via LLVM inline asm (`mov rax, 60; syscall`), same pattern as print_string/print_byte.
- **Rewrote `stubs/core/os/os.odin`**: Changed from `foreign libc { exit :: proc "c" (code: int) --- }` to a plain Odin procedure that calls `os_exit(code)` — no more `foreign libc` dependency.
- **Fixed inline asm constraint length**: Length parameter (33) must match the constraint string exactly; off-by-3 caused truncated constraint `~{r1` and linker crash.
- **Remaining `foreign libc` callers**: `stubs/src/mem/mem.odin` (malloc/free) and `stubs/src/os/os.odin` (exit/getenv/system) — both are dead code (never imported by any test or real program).
- **All 106 tests pass**.

### Deleted dead `stubs/src/` directory
- **Identified dead code**: `stubs/src/os/os.odin`, `stubs/src/mem/mem.odin`, `stubs/src/builtin/builtin.odin`, `stubs/src/intrinsics/intrinsics.odin`, `stubs/src/runtime/runtime.odin` — no resolution path reaches them.
- **Updated `test_stub_import.odin`**: Removed dead `import "mem"` that only worked through the deleted path.

### Implemented low-hanging fruit features
- **`#no_bounds_check` directive grammar**: Added `KwNoBoundsCheck` lexeme, added to `DirectiveName` alternatives. No-op at IR level (bounds checking not yet implemented). Test: `test_no_bounds_check.odin`.
- **`#partial switch` test**: Grammar already supported `Directive?` on `SwitchStatement`; added test verifying switch without default compiles and runs. Test: `test_switch_partial.odin`.
- **`#soa` standalone directive**: Grammar changed to accept `Directive | DirectiveWithArgs` in `SoaType`. Semantic analyser handles `#soa` without `[N]` by creating slice-backed SOA type (each field `T` → `[]T`). Test: `test_soa_simple.odin`.
- **`odinc run` command**: New subcommand that compiles, links, and executes in one step using temp files (cleaned up after execution). Reports exit code.
- **Housekeeping**: Updated `unsupported_features.md` — moved `typeid_of(T)`, `bit_set[u32]`, `contextless` from "To Do" to "Recently Added"; marked `#partial`, `#no_bounds_check` as `✅ GRAMMAR DONE`.
- **All 109 tests pass**.

## Accomplishments (session 2026-07-09)

### Phase 6: Extended formatting, escape sequences, `%u`
- **Extended escape sequences in `ir_gen_string_literal`**: Added `\a` (0x07), `\b` (0x08), `\e` (0x1B), `\f` (0x0C), `\v` (0x0B), `\'` (0x27), `\0` (0x00), `\xNN` (hex escape). Split dispatch to pass `process_escapes` flag — raw strings (backtick) skip escape processing.
- **Rune literal escape handling**: Rewrote `ir_gen_rune_literal` to handle all escape sequences instead of broken `strtoull` fallback.
- **`%u` format specifier**: Added to `printf` in `stubs/core/fmt/fmt.odin`. Delegates to `print_value` for proper unsigned type dispatch.
- **Extended `print_value`**: Added `u16`, `u32`, `u64` cases; `println` now uses `print_value` for unified type dispatch. `u8` and `byte` updated — `byte` uses `print_byte`, `u8` uses `int_to_string`.
- **Tests**: `tests/test_escape.odin` (47 subtests covering all escapes, hex, mixed, raw strings). `test_fmt.odin` extended with `%u` and unsigned type coverage.
- **Cleaned up `notes/unsupported_features.md`**: Removed supported features, ordered remaining by complexity.
- **All 101 tests pass**.

### Implemented Maybe(T) optional type
- **Grammar**: Added `KwMaybe` keyword, `MaybeType` rule (`KwMaybe LParen TypePrefix RParen`), added to `TypePrefix`/`AllReservedWords`.
- **AST**: Added `AST_NODE_MAYBE_TYPE` enum entry, action, and node name.
- **Type descriptors**: Added `TD_KIND_MAYBE` with `get_or_create_maybe_type` — LLVM layout `{i64 tag, T payload}`.
- **Semantic analyser**: Resolves `Maybe(T)` type, `.value` member access (returns inner type), `x.(T)` type assertion (validates target matches inner type), `or_else` result type unwraps `Maybe(T)` → `T`.
- **IR generator**: 
  - Lvalue `.value`: GEP to field 1, bitcast to inner type pointer. 
  - Rvalue `.value`: GEP + load.
  - `or_else`: tag check (field 0 != 0 → none → RHS, else payload), phi-based merge.
  - Type assertion: tag check + trap on fail, payload extraction via GEP.
  - Variable decl: `none` init sets tag=1; implicit `T→Maybe(T)` wrapping stores payload.
  - Fixed: `init_contains_none` helper for detecting `none` inside `AssignExpression` wrapper.
  - Fixed: `TD_KIND_MAYBE` added to composite types list so identifiers return pointer.
  - Fixed: `LLVMBuildStore` NULL guard for general case.
- **Fixed semantic analyser's `or_else`**: Now unwraps `Maybe(T)` to inner type for result type.
- **Fixed `is_type_node`**: Added `AST_NODE_MAYBE_TYPE`.
- **All 86 tests pass** (test_maybe.odin added with 5 subtests).

## Accomplishments (session 2026-06-19)

**Morning session:**
- **Implemented `in`/`not_in` operators**: Rewrote `ir_gen_in_expression` with clean LLVM control flow (entry → loop → body → incr → found/notfound → merge with phi). Fixed GEP issue with slice structs by switching from GEP to Load+ExtractValue approach. Both arrays and slices work as RHS containers. Tests cover found/not-found for arrays and slices in both `if` conditions and assignments.
- **All 38 tests pass** (test_in.odin added with 11 subtests).

**Afternoon session:**
- **Implemented for-range loop codegen (`for i in expr { body }`)**: Discovered that grammar rules without `@AST_ACTION` annotations flatten their children into the parent (confirmed in `easy_pc_ast.c:341-360`). For `for i in 0..10 { body }`, the `ForStatement` handler receives `[Identifier("i"), Expression(range), CompoundStatement(body)]`. The semantic analyser detects for-range (first child is raw `AST_NODE_IDENTIFIER`, range expression resolves to `TD_KIND_RANGE`), then declares the loop variable as `i64` in the loop scope. The IR generator emits: entry (eval range, extract low/high from struct via alloca+GEP+load) → init loop var → cond (cmp < high) → body → inc (loop var++) → cond. Continue target is the increment block; break target is the end block. Tests cover both half-open (`0..<10`) and inclusive (`1..5`) ranges.
- **Implemented two-variable for-range (`for i, val in expr { body }`)**: Extended the for-range detection to collect all identifier children into an array. The IR generator allocates separate storage for each variable, initializes all to the range low value, and increments all on each iteration. Both variables receive the same loop value (for range expressions). Tests cover `for i, val in 0..<10`.
- **All 40 tests pass**.

## Accomplishments (session 2026-06-21)
- **Implemented `bit_field` type**: Added `KwBitField` lexeme, `BitFieldType`/`BitFieldField`/`BitFieldFieldList` grammar rules with AST actions. Semantic analyser resolves bit_field types (computes total bits, picks i8/i16/i32/i64 backing, deduplicates via `get_or_create_bit_field_type`). IR generator allocates backing storage, handles member access (read field: lshr+and; write field: read-modify-write with mask/shift/or). Added `TD_KIND_BIT_FIELD` to type descriptor union with `bit_field_field_info` array.
- **Fixed grammar bug**: `BitFieldFieldList` used `Semicolon` delimiter instead of `Comma` (causing parse error at the variable declaration colon, since the whole `bit_field { ... }` parsed as just the identifier leaving unparseable tokens).
- **Fixed `ir_gen_bit_field_write` unwrapping**: `is_expression_wrapper_type` includes `AST_NODE_POSTFIX_EXPRESSION`, causing the while loop to over-unwrap past PostfixExpression into its children. Added `lhs_expr->type != AST_NODE_POSTFIX_EXPRESSION` guard.
- **Fixed zero-initialization**: Odin semantics require all local variables to be zero-initialized by default (`x: T` = `x: T = 0`). Added `LLVMBuildStore(LLVMConstNull)` after `LLVMBuildAlloca` in `ir_gen_variable_decl` to prevent garbage reads from uninitialized bit_field backing storage.
- **All 45 tests pass** (test_bit_field.odin added with 10 subtests covering basic write/read, field isolation, multi-field, overwrite, and 64-bit backing).

## Accomplishments (session 2026-06-22)
- **Fixed `incl`/`excl` pointer field access bug**: Both `semantic_analyser.c` and `llvm_ir_generator.c` used `ptr_type->element_type` instead of `ptr_type->pointee` (the latter is the correct field for `TD_KIND_POINTER`). Fixed both locations.
- **Fixed bit_set value loading**: `ir_gen_identifier` treated `TD_KIND_BIT_SET` as a "composite type" that returns the alloca pointer instead of loading the integer backing value. This caused `LLVMBuildNot` (in the `-`/AND-NOT operator) to crash on pointer types. Removed `TD_KIND_BIT_SET` from the composite type list so bit_set variables are properly loaded as integer values.
- **Implemented range-based bit_set (`bit_set[0..<32]`)**: Added `AST_NODE_BIT_SET_RANGE` node type, `BitSetRange` grammar rule (`LogOrExpression (DotDotLt | DotDot) LogOrExpression`), `DEFINE_TERMINAL_ACTION` to capture range text, and semantic handling that extracts low/high bounds, determines inclusive/exclusive from the text, calculates bit count, and selects the smallest backing integer (u8/u16/u32/u64).
- **All 49 tests pass** (test_incl_minimal.odin, test_bit_set_ops.odin with 12 subtests, test_bit_set_range.odin with 5 subtests added).
- **Saved CLI redesign analysis** to `cli_redesign_notes.md` for future reference.

## Accomplishments (session 2026-06-23)
- **Phase 4: Caller context threading**: Modified `AST_NODE_POSTFIX_CALL` in `ir_gen_postfix_expression` to prepend the caller's context alloca as the first arg to ODIN-convention calls. Falls back to a fresh zero-initialized context alloca when `context` is not in scope (top-level init code).
- **Phase 5: Entry point wrapper**: `ir_generate()` detects `main` with hidden context param, renames it to `__odin_main` (private linkage), creates C-compatible `int main()` that allocates zero-initialized Context, calls `__odin_main(ctx_ptr)`, truncates `i64`→`i32`, returns.
- **Fixed LLVM 18 opaque pointer issue**: Used `LLVMGlobalGetValueType()` instead of `LLVMTypeOf()` to get function type from a function value (LLVM 15+ opaque pointers make function value types return `ptr`, not function type).
- **All 49 tests pass**.

## Accomplishments (session 2026-06-24)
- **Fixed foreign block test**: `test_foreign.odin` had a type mismatch — `abs` returns `i32` but `main` returns `int` (i64). Added `cast(int) (result - 5)`.
- **Fixed implicit integer coercion in binary expressions**: `ir_gen_binary_expression` now inserts `LLVMBuildIntCast2` when RHS type differs from LHS type for arithmetic/comparison ops (skips `in`/`not_in`/`range` where types naturally differ). Fixes the `sub i32, i64` crash with foreign libc calls.
- **All 55 tests pass**.

## Accomplishments (session 2026-06-29)
- **Implemented `when` declarations**: Added grammar support (`WhenBody` rule with `@AST_ACTION_WHEN_BODY`, `WhenDecl` updated). Semantic analyzer pass 1 registers decls inside matching when/else branches; pass 2 analyzes bodies. IR generator evaluates conditions at compile time with expression-chain unwrapping, processes matching bodies, emits non-procedure constants via `ir_gen_top_level_decl` by evaluating and storing LLVM values in the symbol table.
- **Fixed `sem_evaluate_constant_bool`/`ir_gen_evaluate_constant_bool`**: Added expression-chain unwrapping (including `POSTFIX_EXPRESSION` which has 2 children) so `when true`/`when false` conditions are properly evaluated instead of falling through switch with UB.
- **Fixed `ir_gen_top_level_decl`**: Extended to handle non-procedure top-level constants (`X :: 100`) by evaluating the value expression and storing the LLVM constant in the symbol table via `generator_add_symbol`. Previously only handled procedure literals.
- **All 63 tests pass** (test_when_decl.odin added with `when true`/`when false`/`when false...else` branches).

## Accomplishments (session 2026-07-01)

### SOA structs and `#soa` directive fix
- **Implemented SOA structs (`struct #soa { x: T; y: U }`)**: Added `TD_KIND_SOA` type descriptor with slice-backed fields. Semantic analyser transforms each field type `T` → `[]T` for SOA structs. IR generator allocates struct-of-slices LLVM type and handles field access via GEP.
- **Fixed `#soa` directive detection**: The `lexeme("#" DirectiveName)` parser captures trailing whitespace in its semantic content, producing text `"#soa "` (len 5) instead of `"#soa"` (len 4). Changed `strcmp(child->text, "#soa")` to `strstr(child->text, "#soa") != NULL` in `semantic_analyser.c:817`.
- **All 65 tests pass** (test_soa.odin with 3 `len(s.x)` calls on SOA struct fields).

### Package imports (session 2026-07-01 continued)
- **Named imports (`import alias "path"`)**: Added `AST_NODE_IMPORT_NAMED` grammar rule (`KwImport Identifier StringLiteral`). Semantic analyser extracts alias name from `children[0]` and overrides `pkg->package_name`. Tested with `test_import_named.odin`.
- **Using imports (`import using "path"`)**: Added `AST_NODE_IMPORT_USING` grammar rule (`KwImport KwUsing StringLiteral`). Semantic analyser runs pass1/pass2 on the imported package then copies all symbols from the package scope into the current scope via `generic_hash_table_iterate`. Tested with `test_import_using.odin`.
- **Recursive imports**: Tested chain `main → a → b` — three-level transitive imports work correctly.
- **Import cycle detection**: Added `import_stack` to `SemContext` (dynamic array of resolved paths). Before parsing an import, checks if the resolved path is already in the stack; if so, prints error and aborts. Push/pop wraps each import's parse+analyse cycle. Tested with `expected_to_fail/test_import_cycle.odin` (a↔b cycle).
- **ODIN_ROOT resolution hardening**: `resolve_odin_root` now calls `realpath` on the exe path first to resolve symlinks and relative paths before computing `<exe_dir>/../..`.
- **All 69 tests pass**.

## Accomplishments (session 2026-07-02)

### Import codegen cross-package function calls
- **Fixed build**: Added `import_using_copy_symbol` as a static function in `llvm_ir_generator.c` to resolve linker error (previously only existed as `static` in `semantic_analyser.c`).
- **Implemented cross-package function calls**: Package-qualified member access (`pkg.func()`) now works end-to-end. The semantic analyser's `POSTFIX_EXPRESSION` handler recognizes package-qualified names and resolves members via the package scope. The IR generator's `POSTFIX_MEMBER` handler reads `op->resolved_symbol` when `cur_type` is NULL, and `POSTFIX_CALL` falls back to `cur_type` when the function isn't in the local scope. Tested with `test_import_func_call.odin` (calls `test_import_helper.helper_func(41)` → returns 42).
- **`import using` with function calls**: Works via the symbol re-copy loop in `ir_generate()` that refreshes LLVM values after import codegen. Tested with `test_import_using_func_call.odin` (calls `helper_func(41)` directly).
- **Fixed test bug**: `assert(...)` is a compile-time directive `#assert[...]`, not a runtime function. Fixed both new tests to use return-value checking instead.
- **All 72 tests pass**.

## Accomplishments (session 2026-07-03)

### Better error reporting with source locations
- **Populated `source_data.view` in all AST nodes**: Modified `make_node`, `make_node_base`, and `ast_action_struct_field_action` in `odin_grammar_ast_actions.c` to call `epc_cpt_node_get_input_view(node)` and store line/column info on every AST node during construction.
- **Added file path tracking to semantic errors**: Added `file_path` field to `SemError` struct, updated `sem_error_list_add` signature to take a file path parameter. All 25+ call sites in `semantic_analyser.c` updated to pass `ctx->source_file_path`.
- **Per-file error tracking for imports**: During import processing, `ctx->source_file_path` is temporarily swapped to `pkg->source_path` so errors from imported files correctly show the imported file's path.
- **Improved `sem_error_list_print`**: Now outputs `error: <file>:<line>:<col>: <message>` instead of the bare `Semantic error: <message>`.
- **Same improvements to IR gen errors**: Added `file_path` to `IrGenError` struct and `IrGenContext`, updated the single call site and print function.
- **Removed redundant headers**: Removed `"Semantic analysis failed:"` and `"IR generation failed."` prefix lines since the new format already starts with `error:`.
- **Converted `fprintf` import errors**: Changed three `fprintf(stderr, "Error: Cannot resolve import...")` calls to use `sem_error_list_add` for consistent formatted output.
- **Updated `unsupported_features.md`**: Moved `---` bodyless procedures to "Recently Added" section; added error reporting improvement.
- **All 73 tests pass**.

## Accomplishments (session 2026-07-03, continued)
- **Added error reporting for ~30 silent-failure sites in IR generation**: Added `ir_gen_error_collection_add` calls at ~30 locations across `ir_gen_lvalue`, `ir_gen_postfix_expression`, `ir_gen_node`, `ir_gen_binary_expression`, `ir_gen_unary_expression`, and `ir_gen_in_expression`. Covers P0 (silently-skipped operations on wrong types), P1 (symbol/type lookup failures including cast/transmute/len/make/new/incl/excl), and P2-P4 (tracked in `notes/ir_gen_error_reporting.md`).
- **Handled false-positive edge cases**: Package names in expression context return NULL from `ir_gen_identifier` without error (PostfixMember handler resolves via `op->resolved_symbol`). Blank identifier `_` returns NULL from `ir_gen_lvalue` without error. Outer PostfixExpression node does not add cascading error when inner `ir_gen_lvalue` already reported the cause.
- **All 73 tests pass**.
- **All 89 tests pass**.

## Accomplishments (session 2026-07-05, continued)

### Implemented `..any` variadic parameters
- **Grammar**: Added `VariadicMarker = DotDot @AST_ACTION_ELLIPSIS` wrapper rule. Updated `Parameter` rule: `Identifier Colon VariadicMarker? TypePrefix`.
- **Semantic analyser**: Detects `AST_NODE_ELLIPSIS` child inside `AST_NODE_PARAMETER` from `..` marker; resolves `..any` to `[]any` via `get_or_create_slice_type`.
- **LLVM function type fix**: `LLVMFunctionType` only gets `is_variadic = true` for bare `...` or C convention. `..any` uses a fixed `[]any` slice param, not LLVM varargs. Distinguishes by checking whether last param type is `TD_KIND_SLICE`.
- **Call-site packing**: Extra args packed into `[]any` slice (backing array of `any` structs, each packed via `ir_gen_pack_any`; slice struct built with `LLVMBuildInsertValue`). Distinguishes `..any` from bare `...` (no packing for bare `...`).
- **All 89 tests pass** (test_variadic_any.odin added).

## Summary
- **Maybe(T)** optional type with `.value`, `x.(T)`, `or_else`, `none` init.
- **`..any` variadic parameters**: grammar, semantic analysis (resolves to `[]any`), call-site packing into slice.
- **`type_of` runtime extraction**: `type_of(v: any)` now returns the runtime type_id from the any struct's field 1, enabling runtime type dispatch.
- **`core:fmt` package with `fmt.println`**: Works with both `int` and `string` arguments via `type_of`-based dispatch.
- **All 97 tests pass** (`test_variadic_type_assert` has pre-existing semantic return-type bug).

## Accomplishments (session 2026-07-06)

### Fixed `any` type system — 6 bugs
1. **Package-qualified calls missing arg evaluation**: The semantic analyser's package-qualified `POSTFIX_CALL` handler (for `pkg.func(args)`) never evaluated call arguments, leaving `resolved_type` NULL on all args. Added `sem_evaluate_expr` loop.
2. **Comma-chain right operands unevaluated**: `AST_NODE_EXPRESSION` handler in `sem_evaluate_expr` only recursed into `children[0]` (leftmost leaf), missing all right operands in `chainl1` comma chains. Changed to loop over all children.
3. **`inttoptr` in any packing**: `ir_gen_pack_any` used `inttoptr` for integer args, storing the integer value AS a pointer (address = value). Fixed: alloca+store for all non-pointer types. Same fix in `ir_gen_variable_decl` for non-`any`→`any` assignments.
4. **Type assertion stored pointer directly**: When `val` was a pointer (from subscript GEP), `LLVMBuildStore(val, tmp_alloca)` stored the pointer address into the `any` struct's data field instead of loading the value first. Fixed by adding a load-before-store check.
5. **`inttoptr→ptrtoint` in assertion**: Fixed the integer-extraction path from `ptrtoint` (converts data ADDRESS to int) to `bitcast+load` (loads the VALUE at the data address).
6. **`type_of` returned compile-time type**: `type_of(v)` where `v: any` returned the type_id of `any` itself, not the runtime type stored in the struct. Added runtime field extraction for `any` operands.

### Updated `stubs/core/fmt/fmt.odin`
- `println(args: ..any)` now handles both `int` (type_id 8) and `string` (type_id 45) arguments via `if type_of(v) == 8 { ... } else { ... }` dispatch.

### Test results
- 5 previously-failing tests now pass: `test_core_import`, `test_fmt_int`, `test_fmt_simple`, `test_variadic_assert`, `test_variadic_print`.
- Full suite: **97/98 pass** (only `test_variadic_type_assert` fails — pre-existing semantic return-type mismatch for type assertion in `-> bool` function).

## Accomplishments (session 2026-07-10)

### `@(private)`, `@(link_name)`, `@(require_results)` attributes
- **`@()` attribute grammar**: Added `AttrValue`, `AttrItem`, `AttrList`, `Attribute` grammar rules.
- **New AST nodes**: `AST_NODE_ATTRIBUTE`, `AST_NODE_ATTR_LIST`, `AST_NODE_ATTR_ITEM`.
- **`sem_analyse_attributes()`**: Extracts `link_name`, `require_results`, `is_private` into `ProcDeclAttributes` on `ConstantDecl` metadata.
- **`@(link_name="...")`**: IR gen uses attribute string for `LLVMAddFunction` name.
- **`@(require_results)`**: Parsed and stored (warning emission needs infra).
- **`@(private)`**: `sem_set_symbol_private` marks symbols; cross-package member access checks `sym->is_private`; `import_using_copy_symbol` skips private symbols in both `semantic_analyser.c` and `llvm_ir_generator.c`.
- **Tests**: `test_attribute.odin` (pass), `test_private_local.odin` (pass), `test_private_external.odin` (expected fail). All 105 tests pass.

### Recursive function call fix — fibonacci.odin
- **Link error `undefined reference to 'fib.4'`**: `ir_gen_top_level_decl` and `ir_gen_nested_procedure_decl` stored function value in symbol table AFTER body generation. Recursive calls inside body found `sym->value.value == NULL`, triggering spurious `LLVMAddFunction("fib", ...)` which created `fib.4` (LLVM uniquification). Fixed by moving `generator_add_symbol` before `ir_gen_node` in both functions.
- **Forward-declaration robustness**: Changed `LLVMAddFunction` to first try `LLVMGetNamedFunction` in `ir_gen_identifier`, preventing duplicate forward declarations.
- **Illegal instruction in `printf`**: `%d` handler type-asserted argument is `int`; `u64` argument caused `llvm.trap()`. Fixed by delegating `%d` and `%x` to `print_value` in stubs `fmt.odin`.

### Recursive call semantic fix — variable decl init
- **Root cause of `r: u64 = fib(v - 1) + fib(v - 2)` returning 0**: Top-level procedures registered with `type_info = NULL` during pass1; symbol table only updated with correct proc type AFTER body analysis. Recursive calls found `type_info = NULL`, causing `sem_evaluate_expr` to return NULL, so `resolved_type` was NULL. The IR generator's `init_node` check `child->resolved_type` failed → skipped init generation → variable stayed zero-filled.
- **Fix**: Added `proc_name` parameter to `sem_analyse_procedure_literal`. Symbol table is updated with the full procedure type BEFORE body analysis via `scope_add_symbol`.
- **`fibonacci.odin`**: Now uses clean code with zero casts — `return 1` and `return fib(v - 1) + fib(v - 2)` both work. Output: `fib(6) = 13`.

### Implicit conversion of untyped literals
- **`sem_can_implicitly_convert()`**: New function that checks whether an untyped integer/float literal can be implicitly converted to a target numeric type. Unwraps expression wrapper chains to find the underlying `AST_NODE_INTEGER_VALUE` or `AST_NODE_FLOAT_VALUE`. Used in `sem_analyse_return_statement` (both single and multi-return checks).
- **IR coercion for return values**: Added integer type coercion in `ir_gen_return_statement` — when the generated return value's LLVM type doesn't match the function's return type, a `LLVMBuildIntCast2` is inserted.
- **Tests**: `test_implicit_conv.odin` covers recursive fib with var-decl init, literal-to-u8/u16/u32/u64/i32/i64/f64, and u64+u64 addition.

### Centralized type coercion (`coerce_value_to_type`)
- **New function** `coerce_value_to_type(ctx, value, target_type, src_is_unsigned, name_hint)` handles all numeric type conversions in one place: integer↔integer (trunc/zext/sext), float↔float (fptrunc/fpext), integer↔float (sitofp/uitofp/fptosi/fptoui), pointer↔integer (ptrtoint/inttoptr).
- **Refactored `ir_gen_auto_cast_value`** to delegate to `coerce_value_to_type`, eliminating duplicate conversion logic.
- **Three implicit coercion sites refactored**: variable declaration initialization (`ir_gen_variable_decl`), return statement (`ir_gen_return_statement`), and binary expression RHS (`ir_gen_binary_expression`) — all now call `coerce_value_to_type` instead of inline `LLVMBuildIntCast2`/`LLVMBuildFPCast`.
- All 106 tests pass.

### `core:os` support — `os.exit()` and void-only main
- **`stubs/core/os/os.odin`**: New file — `exit` declared as `proc "c" (code: int) ---` via foreign libc.
- **Entry point wrapper updated**: `ir_generate()` always returns 0 from C `main()`. Exit code set via `os.exit()` calls (which terminate before the wrapper's `ret`).
- **Batch conversion script** (`scripts/convert_tests.py`): Mechanically transforms all test files from `main :: proc() -> int { return X }` to `main :: proc() { os.exit(X) }`. Handles inline `{ return X }` patterns, skips nested proc returns, skips `return` in comments.
- **Semantic error for non-void main**: `AST_NODE_CONSTANT_DECL` handler now checks if a `main` procedure has a non-void return type and emits a clear error.
- **`tests/test_param.odin` deleted**: Incompatible with void-only main; coverage provided by `test_proc_params.odin` and `test_call.odin`.
- **All 105 tests pass**.

### Extended `core:fmt` variants
- **Added `printfln`, `eprintln`, `eprintf`, `eprintfln`**: Each is a standalone inline copy of the format/print logic (no `..args` forwarding or `[]any` param delegation — both unsupported by the compiler). Text: `test_fmt_more.odin` covers all four.
- **Test runner**: `bash tests/run_tests.sh` confirms all 106 tests pass.
- **Root cause of delegation failure**: `ir_gen_identifier` returns alloca pointers for composite types (slices, structs, etc.) at line 291. When passed as function call arguments, the IR receives `ptr %args` instead of loading `{ptr, i64}`. Fix requires either loading composite type values at call sites or a separate argument evaluation path.

## Accomplishments (session 2026-07-14, continued)

### Implemented `@(builtin)` — Builtin annotation
- **`ProcDeclAttributes`** (`type_descriptors.h`): Added `bool is_builtin` field.
- **`sem_analyse_attributes()`** (`semantic_analyser.c`): Parses `@(builtin)` → sets `attrs->is_builtin = true`.
- **`ir_gen_top_level_decl()`** (`llvm_ir_generator.c`): Uses `is_builtin` as primary signal for intrinsic body generation. Known intrinsics dispatch via name-based `ir_gen_runtime_intrinsic_body()`. Unknown builtins emit compile-time error.
- **`stubs/core/runtime/runtime.odin`**: All 8 runtime intrinsic declarations now use `@(builtin)`.
- **Tests**: `test_builtin.odin` uses `@(builtin)` with `print_string`/`int_to_string`/`os_exit`. All 124 tests pass.
- **Root cause**: The `delimited` combinator in strict mode (`delimited_flex` available at `easy_pc/lib/parsers.c:2875`) errors on trailing separators instead of backtracking over them. The `Semicolon?`/`Comma?` after every `delimited` in the grammar was dead code — never reached.
- **Fix**: Changed all 8 `delimited(X, Sep)` calls in `odin_grammar.gdl` to `delimited_flex(X, Sep)`. `delimited_flex` backtracks over the trailing separator, then `Sep?` consumes it. This fixes `:: struct { x: int; }`, `enum { A; B; }`, `union { x: int; y: int; }`, `@(a, b,)`, `bit_field { a: 0; b: 1; }`, etc.
- **Tests**: `test_type_alias_struct.odin` uses `:: struct` with field access. All 123 tests pass.

### Implemented `#align` struct/field alignment
- **Grammar** (`odin_grammar.gdl`): Added `KwAlign` lexeme, added to `DirectiveName`. `StructType` changed to `KwStruct (SoaType | ((Directive IntegerLiteral?)? StructRawBody))`. `StructField` changed to accept `(Directive IntegerLiteral)?` after `TypePrefix`.
- **Semantic analyser** (`semantic_analyser.c`): `AST_NODE_STRUCT_TYPE` handler scans children for `AST_NODE_DIRECTIVE` with text `"#align"` and extracts subsequent `AST_NODE_INTEGER_VALUE`. Overrides `struct_metadata.alignment` after `register_struct_type`. Field-level `#align` extracted and stored in `struct_field_t.user_alignment`.
- **IR generator** (`llvm_ir_generator.c`): `ir_gen_variable_decl` uses `struct_metadata.alignment` for `LLVMSetAlignment` when it exceeds ABI alignment.
- **Pre-existing bug discovered**: `:: struct { ... }` (ConstantDecl with inline struct type) never worked even before changes — `End of input not found` error.
- **Tests**: `test_align.odin` with struct-level and field-level `#align` in variable declarations.
- **All 122 tests pass** (121 previous + 1 new).

### Key insight
The `any` type system had two fundamental flaws: (a) integer arguments were stored as `inttoptr` values (data pointer = integer cast to pointer) instead of storing the integer in memory and pointing to it; (b) the `type_of` builtin only worked at compile time, making runtime type dispatch impossible. Fixing both enabled proper runtime type identification and safe type assertion through the `any` struct's `{ptr data, i64 type_id}` layout.

## Accomplishments (session 2026-07-14)

### Removed name-based intrinsic detection (cleanup)
- **Removed `ir_gen_is_runtime_intrinsic()`**: The name-based detection function was redundant now that all runtime intrinsics use `@(builtin)`. The IR generator's `ir_gen_top_level_decl()` now uses `attrs->is_builtin` as the sole signal for intrinsic body generation.
- **Simplified dispatch**: `body_node || is_builtin` replaces the previous `body_node || is_builtin || is_known_intrinsic` condition. The name-based dispatch inside `ir_gen_runtime_intrinsic_body()` is now only reached from the `@(builtin)` path.
- **Safety**: Added a final `else` clause to `ir_gen_runtime_intrinsic_body()` that emits `LLVMBuildUnreachable` for unknown builtin names (previously fell through with no terminator, causing LLVM verification errors).
- **All 124 tests pass**.

### Implemented runtime bounds checking on array/slice/string subscripts
- **`IrGenContext`** (`llvm_ir_generator.h`): Added `bool bounds_checking_enabled` field, initialized to `true` in `ir_gen_context_create`.
- **`ir_gen_emit_bounds_check()`** (`llvm_ir_generator.c`): Helper that emits `icmp uge index, len` → conditional branch to trap block. Trap block calls `llvm.trap()` + `unreachable`. Continuation block resumes normal flow. Returns the (possibly extended) index value.
- **Forward declaration**: Added `static LLVMValueRef ir_gen_emit_bounds_check(...)` to the forward declarations section to resolve implicit declaration conflict (function used in `ir_gen_lvalue` at line ~1666, defined at line ~3971).
- **Lvalue subscript path** (`ir_gen_lvalue`): For `TD_KIND_ARRAY`, uses `cur_type->as.array.count` as compile-time length constant. For `TD_KIND_SLICE`/`TD_KIND_DYNAMIC_ARRAY`, loads `.len` field (field index 1) via GEP. For `string`, loads `.len` field via GEP. Bounds check emitted before the element-access GEP.
- **Rvalue subscript path** (`ir_gen_postfix_expression`): Same logic. For arrays, uses compile-time count. For slices/dynamic arrays, loads `.len` via GEP. For strings, uses `LLVMBuildExtractValue` on the string struct to get `.len` (field index 1).
- **`#no_bounds_check` directive**: `AST_NODE_DIRECTIVE` handler in `ir_gen_node` now checks for `#no_bounds_check` and sets `ctx->bounds_checking_enabled = false`. This is a simple toggle (once disabled, stays disabled for the rest of the compilation unit).
- **Map subscripts**: Not bounds-checked (use linear-scan lookup, not direct indexing).
- **Tests**: `test_bounds_check.odin` (7 subtests: array read/write, slice read/write, string read — all in-bounds, verify correctness). Updated `test_no_bounds_check.odin` to test OOB array access with `#no_bounds_check` directive (program runs without trapping). Manually verified: OOB access without `#no_bounds_check` produces SIGILL (exit 132) from `llvm.trap()`.
- **All 125 tests pass** (124 previous + 1 new; `test_no_bounds_check.odin` updated from no-op syntax test to functional test).

### Key insight
The `if condition do statement` form is not supported by the grammar (only `if condition { statements }` with braces). This was a pre-existing limitation never exposed by tests. The `arr[:]` full-slice syntax is also unsupported — only `arr[..]` works.

## Accomplishments (session 2026-07-14, continued)

### Implemented `if cond do stmt` grammar form
- **Grammar** (`odin_grammar.gdl`): Added `KwDo = lexeme("do" IdBoundary)` keyword. Added `KwDo` to `AllReservedWords` (prevents `do` from being used as an identifier). Changed `IfStatement` rule from `CompoundStatement` to `(CompoundStatement | (KwDo Statement))` for both then and else branches, allowing `if cond do stmt`, `else do stmt`, `else if cond do stmt` alongside the existing braced form.
- **Semantic analyser** (`semantic_analyser.c`): The `AST_NODE_IF_STATEMENT` handler previously only handled `AST_NODE_COMPOUND_STATEMENT` children for scope/analysis; other children fell through to `sem_evaluate_expr`, which broke for statement nodes (`os.exit()`, etc.). Added explicit dispatch for statement-type children (`AST_NODE_EXPRESSION_STATEMENT`, `AST_NODE_ASSIGN_STATEMENT`, `AST_NODE_VARIABLE_DECL`, `AST_NODE_RETURN_STATEMENT`, etc.) to `sem_pass2_node` with scope push/pop.
- **IR generator**: No changes needed — `ir_gen_if_statement` already uses `ir_gen_node` for branch bodies, which dispatches correctly for both `CompoundStatement` and `ExpressionStatement`.
- **Tests**: `test_if_do.odin` (basic if-do), `test_if_do_else.odin` (if-do + else-if-do + else-do + mixed braces).
- **All 128 tests pass** (125 previous + 3 new).

### Implemented `arr[:]` full-slice syntax
- **Grammar** (`odin_grammar.gdl`): Added `PostfixOpFullSlice = LBracket Colon RBracket @AST_ACTION_POSTFIX_SLICE` rule. Added `PostfixOpFullSlice` to `PostfixOp` alternatives. Reuses the same AST action as `PostfixOpSlice`, so `arr[:]` produces an `AST_NODE_POSTFIX_SLICE` node identical to `arr[..]`.
- **No semantic/IR changes needed**: The existing `POSTFIX_SLICE` handling in semantic analyser and IR generator already treats both `arr[..]` and `arr[:]` identically (full slice with `low=0, high=array.count`).
- **Tests**: `test_arr_full_slice.odin` (3 subtests covering `arr[:]` with explicit type, `:=` shorthand, element access).
- **All 128 tests pass** (no regressions).

### Key insight (this session)
The `@AST_ACTION_POSTFIX_SLICE` action is shared between `PostfixOpSlice` (for `arr[..]`, `arr[low..high]`, etc.) and the new `PostfixOpFullSlice` (for `arr[:]`). Both produce `AST_NODE_POSTFIX_SLICE` nodes, so the semantic analyser and IR generator handle them identically without needing to distinguish the source syntax.
