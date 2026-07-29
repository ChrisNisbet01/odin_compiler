# Plan: `c` Variant fmt procs (caprint, ctprintf, etc.)

## Goal
Add `core:fmt` `c` variant procs that return `cstring` (null-terminated C strings):
- `caprint(args, sep, allocator) -> cstring` — formatted C string, allocator
- `caprintf(format, args, allocator, newline) -> cstring` — formatted C string via format, allocator
- `caprintfln(format, args, allocator) -> cstring` — caprintf with newline
- `ctprint(args, sep) -> cstring` — formatted C string, temporary allocator
- `ctprintf(format, args, newline) -> cstring` — formatted C string via format, temporary allocator
- `ctprintfln(format, args) -> cstring` — ctprintf with newline

## Official Odin API

```odin
caprint  :: proc(args: ..any, sep := " ", allocator := context.allocator) -> cstring
caprintf :: proc(format: string, args: ..any, allocator := context.allocator, newline := false) -> cstring
caprintfln :: proc(format: string, args: ..any, allocator := context.allocator) -> cstring
ctprint    :: proc(args: ..any, sep := " ") -> cstring
ctprintf   :: proc(format: string, args: ..any, newline := false) -> cstring
ctprintfln :: proc(format: string, args: ..any) -> cstring
```

All use `strings.Builder`, `sbprint`/`sbprintf`, append a null byte via `strings.write_byte(&str, 0)`, call `strings.to_string(str)`, then return `cstring(raw_data(s))`.

## Issues Blocking Full Compatibility

### Issue 1: `..any` followed by named default params in grammar

**Problem**: The grammar `Parameter` rule is:
```
Parameter = KwUsing? ((PolyIdent Colon TypePrefix) | (Identifier Colon VariadicMarker? TypePrefix)) (ColonAssign AssignExpression)?
```

This requires `Identifier Colon TypePrefix` — the type MUST be specified. A parameter like `sep := " "` (type inferred from default) parses-fails because `Colon` is expected but `ColonAssign` is found.

**Fix**: Extend the `Parameter` rule to allow type-inferred defaults:
```
Parameter = KwUsing? ((PolyIdent Colon TypePrefix) | (Identifier Colon VariadicMarker? TypePrefix (ColonAssign AssignExpression)?) | (Identifier ColonAssign AssignExpression)) @AST_ACTION_PARAMETER
```

This adds a third alternative: `Identifier ColonAssign AssignExpression` (type inferred from default value).

**Files**: `src/odin_grammar.gdl` (grammar), `src/semantic_analyser.c` (semantic analysis: infer type from default value expression), `src/llvm_ir_generator.c` (IR: handle the inferred type in procedure signatures).

### Issue 2: `..any` + named defaults break IR call argument packing

**Problem**: When a function has `proc(args: ..any, sep: string := " ")`:
1. At the call site `test_proc(1, 2, 3)`, the compiler needs to:
   - Pack `1, 2, 3` into a `[]any` slice for `args`
   - Fill `sep` with its default `" "`
2. But our current default param filling iterates by **position** — it fills missing args at positions `count..param_count` with defaults. With `..any`, the param count is dynamic and the "fill remaining" logic breaks.

**Fix**: Improve `ir_gen_postfix_call` to:
- Know which parameters have `..any` (variadic) type
- Wrap extra call args into a `[]any` slice up to the position before the last named params
- Fill named defaults for params that weren't provided args

**Files**: `src/ir_gen_postfix.c` (`ir_gen_postfix_call`), `src/semantic_analyser.c` (semantic: set `resolved_type` on variadic-named param combo).

### Issue 3: `context.allocator` as default for `Allocator` type

**Problem**: When the default value is `context.allocator`, the parser must evaluate `context.allocator` (which is `Expression`). Our default param parser expects `AssignExpression`, but `context.allocator` IS an AssignExpression (it's an Identifier with member access).

This might already work since `context.allocator` is just `PostfixExpression(Identifier("context"), PostfixMember("allocator"))`.

**Risk**: If the semantic analyser doesn't register ` contex.allocator` properly or the Allocator type isn't defined, this will fail.

### Issue 4: `raw_data(s)` on string — DONE

Fixed in this session. `raw_data(s)` where `s: string` now returns `^u8` pointer to the string's data buffer.

### Issue 5: `cstring(raw_data(s))` cast — DONE

Already works via `TypeCastByJuxtaposition = TypeName LParen Expression RParen`.

## Work Required for Full Compatibility

1. **Grammar**: Allow `Identifier ColonAssign AssignExpression` in `Parameter` rule (type inferred from default)
2. **Semantic**: When a parameter uses `:= default`, infer the type from the default expression's type
3. **Semantic**: Ensure `context.allocator` and `context.temp_allocator` are recognized as `Allocator` type
4. **IR**: Fix variadic `..any` args + named defaults to fill named defaults after packing variadic args
5. **Stubs**: Add `builder_init` to `strings` (initializes an existing Builder with allocator)
6. **Stubs**: Update `sbprint` to accept `sep: string := " "` and `sbprintf` to accept `newline: bool := false`
7. **Stubs**: Add `caprint`, `caprintf`, `caprintfln`, `ctprint`, `ctprintf`, `ctprintfln` to `fmt.odin`
8. **Tests**: `test_c_fmt.odin` covering all 6 procs

## Simplified Variants (do first)

Implement without `sep`/`newline`/`allocator` named params, since `..any` + named defaults don't work yet:

```odin
ctprintf    :: proc(format: string, args: ..any) -> cstring
ctprintfln  :: proc(format: string, args: ..any) -> cstring
caprint     :: proc(args: ..any) -> cstring  // uses space sep, default allocator
caprintf    :: proc(format: string, args: ..any) -> cstring
caprintfln  :: proc(format: string, args: ..any) -> cstring
ctprint     :: proc(args: ..any) -> cstring
```

These cover the most common use case: pass args/format, get a `cstring` back. The `sep` default is already `" "` and `newline` is already `false` in the existing `sbprint`/`sbprintf` implementations.
