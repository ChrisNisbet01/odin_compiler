# CLI Redesign Notes

## Current CLI (`odinc`)

| Command | Args           | Behavior |
|---------|----------------|----------|
| `build` | `<file>`       | Parse → Semantic Analysis → LLVM IR → `.ll` file |
| `check` | `<file>`       | Parse → Semantic Analysis only |
| `version` | (none)       | Print version |
| `help`   | (none)        | Print usage |

- No flags, no options, no output path control
- `run` doesn't exist
- Single file only (no directory/package support)
- Output `.ll` placed next to source file
- Does not invoke `clang` or produce native executables

## Official CLI (`odin`)

### Commands

| Command     | Target  | Description |
|-------------|---------|-------------|
| `build`     | dir     | Compile directory as executable |
| `run`       | dir     | Build + execute |
| `check`     | dir     | Parse + type-check only |
| `test`      | dir     | Build & run `@(test)` procedures |
| `doc`       | dir     | Generate documentation |
| `version`   | (none)  | Print version |
| `report`    | (none)  | Bug report info |
| `root`      | (none)  | Print collections root path |
| `strip-semicolon` | dir | Remove unneeded semicolons |

### Key Flags

| Flag | Purpose |
|------|---------|
| `-out:<path>` | Output path |
| `-o:<style>` | Output style: `minimal`, `size`, `speed`, `aggressive`, `none` |
| `-debug` | Emit debug info |
| `-no-bounds-check` | Disable bounds checking |
| `-linker:<linker>` | Linker to use (`mold`, `radlink`, `lld`, system default) |
| `-microarch:<arch>` | Target microarchitecture (`native`, etc.) |
| `-define:<name>:<value>` | Set `#config` constant |
| `-vet` / `-vet-*` | Extra static checks |
| `-show-timings` | Print compilation time breakdown |
| `-keep-temp-files` | Keep intermediate files |
| `-build-mode:<mode>` | `exe`, `lib`, `shared`, `object` |
| `-collection:<name>=<path>` | Define a package collection |
| `-show-debug-messages` | Dump compiler debug stats |
| `-dynamic-literals` | Allow dynamic literals |
| `-sanitize:<sanitizer>` | Enable sanitizers |
| `-no-crt` | Don't link C runtime |
| `-reloc-mode:<mode>` | `default`, `pic`, `static`, `dynamic-no-pic` |

## Key Differences

### 1. Target is a directory, not a file

Official Odin compiles all `.odin` files in a **directory** as a single package:
```
odin build .          # compile current directory
odin run  my_program/ # compile and run my_program/
```

Our compiler currently takes a single file:
```
odinc build foo.odin
```

**Required change**: Detect whether the argument is a file or directory. If directory, enumerate all `.odin` files inside, parse each, merge into a single `Program` AST.

### 2. `run` command

Official:
```
odin run .   # builds + runs
```

Our compiler has no `run`. The test harness does it:
```
./build/src/odinc build foo.odin
clang foo.odin.ll -o foo
./foo
```

**Required change**: Add `run` command to `main.c` that builds, invokes `clang`, and executes the result.

### 3. Output control

Official: `-out:<path>`, `-o:<style>`

Our compiler: always emits `.ll` in source directory, no control.

**Required change**: Parse `-out:` and `-o:` flags. Implement `-o:llvm-ir` (current default), `-o:none` (check only), `-o:object` and `-o:assembly` (require LLVM emit target).

### 4. No flags at all

Our compiler ignores `argv[3+]`. The official compiler has dozens of flags.

**Required change**: Add flag parsing loop after extracting command + target.

## Minimum Changes to Align

1. **`src/main.c`**: Add `run` command (build + `clang` + execute)
2. **`src/main.c`**: Add `-out:<path>` flag parsing
3. **`src/main.c`**: Add directory-as-target support
4. **`src/main.c`**: Add `-o:<style>` flag
5. **`tests/run_tests.sh`**: Update if CLI syntax changes (e.g. `build .`)

## Deferred / Significant Efforts

| Feature | Effort | Notes |
|---------|--------|-------|
| `-debug` | High | Requires `LLVMDIBuilder` throughout IR generator |
| `-vet` | Medium | New analysis passes in semantic analyser |
| `-define:` | Medium | Requires `#config` constant support in parser/lexer |
| `-linker:` | Low | Just pass different linker name to `clang` invocation |
| `-show-timings` | Low | Add `clock_gettime` calls around pipeline stages |
| `-no-bounds-check` | Medium | Add flag to IR gen context, guard bounds check emission |
| `-build-mode:` | Medium | Link as `.a`/`.so`/`.o` instead of executable |
| `-collection:` | Medium | Add search path logic to import resolution |
| `-microarch:` | Low | Pass `-march`/`-mtune` to clang |
| Multi-file packages | High | AST merging, duplicate checking, inter-file resolution |
