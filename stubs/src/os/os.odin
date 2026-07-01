package os

foreign libc {
    exit :: proc "c" (code: int) ---
    getenv :: proc "c" (name: cstring) -> cstring ---
    system :: proc "c" (cmd: cstring) -> int ---
}

// Odin-style wrappers
EXIT_SUCCESS :: 0
EXIT_FAILURE :: 1
