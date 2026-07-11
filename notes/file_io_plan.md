# File I/O Implementation Plan

## Goal
Add minimal Linux x86-64 file I/O to the Odin compiler: `open`, `read`, `write`, `close` syscalls wrapped as runtime intrinsics with Odin-style OS stubs and handle tracking.

## Architecture
```
User code → os.open/read/write/close (Odin wrappers) → sys_open/read/write/close (runtime intrinsics, --- bodyless) → ir_gen_runtime_intrinsic_body (inline syscall via LLVM asm)
```

## Intrinsic Signatures (in `stubs/core/runtime/runtime.odin`)
- `sys_open(path: string, flags: int, mode: int) -> int`     // returns fd (>=0) or -errno
- `sys_read(fd: int, buf: []byte) -> int`                     // returns bytes read or -errno
- `sys_write(fd: int, buf: []byte) -> int`                    // returns bytes written or -errno
- `sys_close(fd: int) -> int`                                 // returns 0 or -errno

## OS Stubs (in `stubs/core/os/os.odin`)
- `Handle :: int`, `Errno :: int`
- Flag constants: `O_RDONLY=0`, `O_WRONLY=1`, `O_RDWR=2`, `O_CREAT=64`, `O_TRUNC=512`, `O_APPEND=1024`
- Handle tracking: global `[256]int` array + count, helpers `__odin_track_handle`/`__odin_untrack_handle`/`__odin_close_all_handles`
- Wrappers: `open` (calls sys_open, tracks success), `close` (untracks + sys_close), `read` (sys_read), `write` (sys_write)
- `exit` updated to close all handles before `os_exit`

## Test
- `tests/test_file_io.odin`: write "Hello Odin IO!" to `/tmp/odin_io_test.tmp`, read back, verify content. Exit 0 on success.

## Implementation Order
1. Add 4 declarations to runtime.odin
2. Implement sys_write (easiest — follows print_string pattern)
3. Implement sys_close (single-arg syscall)
4. Implement sys_open (string→cstring with [4096]i8 stack buffer + min(len,4095) + memcpy + null-terminator)
5. Implement sys_read (same pattern as sys_write, rax=0)
6. Update os.odin with types, flags, wrappers, handle tracking, updated exit
7. Write test and add to build system
8. Consider refactoring print_string to use sys_write instead

## Potential Issues / Fallbacks
- Global array `[256]int` may not work in current compiler → use close-all-fds loop in exit instead
- Bitwise OR in constant context may not fold → manually compute flag values
- Octal 0o644 may not work → use decimal 420
- `[256]byte` local array may not work → use `make([]byte, 256)` if supported
- `buf[:]` full slice may not work → use `buf[0:256]`
