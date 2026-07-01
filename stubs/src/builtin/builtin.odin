package builtin

alloc :: proc "contextless" (size: int) -> rawptr ---
free :: proc "contextless" (ptr: rawptr) ---
