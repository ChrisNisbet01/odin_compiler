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
- **`fibonacci.odin`**: Fixed format string to use `%d` (now works with `print_value` dispatch), removed redundant `u64()` outer wrapper (inner `u64(1)` still needed for literal type inference). Output: `fib(6) = 13`.

### Key insight
The `any` type system had two fundamental flaws: (a) integer arguments were stored as `inttoptr` values (data pointer = integer cast to pointer) instead of storing the integer in memory and pointing to it; (b) the `type_of` builtin only worked at compile time, making runtime type dispatch impossible. Fixing both enabled proper runtime type identification and safe type assertion through the `any` struct's `{ptr data, i64 type_id}` layout.
