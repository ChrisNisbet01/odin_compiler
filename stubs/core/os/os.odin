package os

foreign libc {
    exit :: proc "c" (code: int) ---
}
