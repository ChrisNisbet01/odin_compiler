# `os.args` Implementation Plan

## Goal
Support `os.args` (a `[]string` package-level variable containing command-line arguments) in the Odin compiler.

## Architecture

### Current State
- C `main()` entry point: `int main()` — no argc/argv, always returns 0
- `__odin_main(context_ptr)`: Odin main with hidden context param
- Context struct: empty (`count = 0`)
- `os` package stubs: only `exit :: proc(code: int) { os_exit(code) }`
- No way to access command-line args from Odin code

### Design

**Store argc/argv in LLVM globals, populate `os.args` in `__odin_main` startup code.**

**Why:** No changes needed to the Odin `main` proc signature or the function type system. The argc/argv globals act as a bridge between the C entry point and the Odin runtime init.

**Flow:**
1. C `main(int argc, char** argv)` stores `argc`/`argv` to globals `@__odin_argc`, `@__odin_argv`
2. `__odin_main(context_ptr)` reads globals at startup, builds `[]string`, stores in `os.args`

## Implementation Steps

### Step 1: Add `args` declaration to os stubs
File: `stubs/core/os/os.odin`
```odin
args: []string
```

### Step 2: Create argc/argv globals
In `ir_generate()`, before the entry point wrapper:
- `@__odin_argc = global i64 0`
- `@__odin_argv = global ptr null`

### Step 3: Update C main() signature and body
In `ir_generate()` at the entry point wrapper:
- C `main()`: `int main(int argc, char** argv)`
- Store `argc` → `@__odin_argc`, `argv` → `@__odin_argv`
- Then allocate context, call `__odin_main`, return 0

### Step 4: Generate runtime init code to populate os.args
In `ir_gen_top_level_decl()`, when processing the `main` proc:
- After context param setup, before user body:
  1. Load `argc` from `@__odin_argc`, `argv` from `@__odin_argv`
  2. If `os` package is imported and has `args` symbol:
     a. `malloc(argc * sizeof(string_struct))` (string_struct = {ptr, i64} = 16 bytes)
     b. Loop through argv[i]:
        i. `strlen(argv[i])` → len
        ii. Build `string{data = argv[i], len = strlen_result}`
        iii. Store at `backing_array[i]`
     c. Build `slice{data = backing, len = argc}`
     d. Store in `os.args` global

### Step 5: Create test
File: `tests/test_os_args.odin`
```odin
package main

import "core:os"

main :: proc() {
    os.exit(len(os.args))
}
```

- Run with no args → exit code 1 (just program name)
- Test with `./test_os_args foo bar` → exit code 3

### Step 6: Run full test suite, verify no regressions

## Dependencies
- `strlen` from libc (declared in LLVM IR like `malloc`)
- `malloc` from libc (already available via `ir_gen_call_malloc`)
- LLVM loop generation (already exists for for-range and `in` operator)

## Edge Cases
- No `core:os` import → skip init code, no crash
- No arguments (argc=0) → create empty slice
- Very large argc → malloc may fail, but we don't check (same as other allocations)
- argv[i] with empty string → strlen returns 0, string{data=argv[i], len=0} (valid)
