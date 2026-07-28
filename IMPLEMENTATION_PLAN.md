# Implementation Plan: io.odin Writer/Stream Support

## Current State

All 208+ tests pass. No uncommitted source changes (only this plan file is untracked).

## Blockers for io.odin

io.odin (`stubs/core/io/io.odin`) fails to parse with `End of input not found` because it
uses two enum-value syntaxes the compiler does not yet support:

### Issue 1: Dot-prefixed unqualified enum values (grammar)

**Used in io.odin:**
```odin
w.procedure(w.data, .Write, p, 0, .Start)   // .Write and .Start are arguments
w.procedure(w.data, .Flush, nil[:], 0, .Start)
return Unsupported                           // bare unqualified name (already works)
```

**Root cause:** `PrimaryExpression` in `src/odin_grammar.gdl:397` has no `.FieldName`
alternative. The `Dot` token is only consumed by `PostfixOpMember`, which requires a
preceding expression to attach to. A bare `.Write` with nothing before it is unparseable.

**Failed attempt:** Adding `| Dot FieldName` to `PrimaryExpression` caused parsing
conflicts — the `Dot` was consumed greedily by PrimaryExpression, starving
`PostfixOpMember` of its `.member` tokens and breaking `os.exit()` (which chains
`.exit` as a member of `os`).

**Possible fixes:**
- (A) Add `Dot FieldName` to PrimaryExpression AND restructure PostfixExpression so
  that `PostfixOpMember` is not starved. This requires understanding exactly why the
  greedy match broke downstream parsing — likely needs the PEG parser to backtrack.
- (B) Rewrite io.odin to avoid `.EnumValue` syntax entirely (use qualified form
  `Error.Unsupported`, `Stream_Mode.Write`, etc.). Simpler but loses idiomatic Odin.
- (C) Handle `.EnumValue` in the lexer as a special token type (e.g. `DotEnumValue`)
  that only fires when followed by a known enum member name. Fragile and circular.

### Issue 2: Qualified enum member access (semantic + IR gen)

**Grammar:** `Error.None` already parses correctly as
`PrimaryExpression("Error") → PostfixOpMember(".None")`.

**Semantic analyzer:** `sem_evaluate_postfix_expr` in `src/sem_evaluate_expr.c`
handles `POSTFIX_MEMBER` for struct, union, bit_field, maybe, vector, string, slice,
dynamic_array, array, pointer — but has **no `TD_KIND_ENUM` case**. When the base
expression resolves to an enum type, the `.None` member falls through to a "type has
no member" error.

**IR generator:** `ir_gen_postfix_member` in `src/ir_gen_postfix.c` similarly has no
`TD_KIND_ENUM` case — the fallthrough at line 810 rejects enum types.

**Fix needed:** Add `TD_KIND_ENUM` handling in both semantic analyzer and IR gen:
- Semantic: look up member name in `type_descriptor->as.enum_type.enumerator_names[]`,
  set `resolved_type` to the enum type.
- IR gen: return the integer constant value for the matched enumerator
  (`LLVMConstInt(enum_llvm_type, enumerator_value)`).

### Issue 3: Bare unqualified enum values in scope (already works)

`B` in `val := cast(int) B` works because `B` is registered as a symbol with
`has_const_int_val = true` during enum type registration. No changes needed.

## Other files affected by Issue 1

- `stubs/core/runtime/internal.odin`: `.Free_All` argument to allocator proc
- `stubs/core/runtime/default_temporary_allocator.odin`: `.Freestanding` in comparison

## Remaining Work (ordered by dependency)

### Step 1: Fix qualified enum member access (Issue 2)
**Files:** `src/sem_evaluate_expr.c`, `src/ir_gen_postfix.c`
- Add `TD_KIND_ENUM` case in the `POSTFIX_MEMBER` chain of `sem_evaluate_postfix_expr`
- Add `TD_KIND_ENUM` case in `ir_gen_postfix_member`
- Test: `Error.None` in a return statement, passing it to a function

### Step 2: Fix dot-prefixed unqualified enum values (Issue 1)
**Files:** `src/odin_grammar.gdl`, possibly `src/ir_gen_postfix.c`
- Decide between approach (A) grammar fix vs (B) rewrite io.odin
- If grammar fix: add `Dot FieldName` to PrimaryExpression, fix the conflict
- If rewrite: change io.odin to use `Error.Unsupported`, `Stream_Mode.Write`, etc.

### Step 3: Implement sys_write syscall for stdout/stderr
**Files:** `src/ir_intrinsic.c` (or `src/ir_gen_runtime_intrinsics.c`)
- Wire `ir_gen_intrinsic_sys_write()` to the stream procedures
- Create `Stream` instances for stdout (fd=1) and stderr (fd=2)

### Step 4: Wire fmt.odin to use io.Writer
**Files:** `stubs/core/fmt/fmt.odin`
- Change `println` to accept `io.Writer` parameter
- Or create `print_to` / `println_to` variants

## Test Status
- `test_enum.odin`: PASS
- `test_fmt.odin`: PASS
- io.odin: **FAILS** (parse error — needs Issue 1 or Issue 2 fix)
