package mem

foreign libc {
    malloc :: proc "c" (size: int) -> rawptr ---
    free :: proc "c" (ptr: rawptr) ---
}
