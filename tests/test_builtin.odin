package main
import "core:os"

@(builtin)
print_string :: proc(fd: int, str: string) ---

@(builtin)
int_to_string :: proc(i: int) -> string ---

@(builtin)
os_exit :: proc(code: int) ---

main :: proc() {
    // Verify @(builtin) intrinsics compile and run correctly
    s := int_to_string(42)
    print_string(1, s)
    print_string(1, "\n")
    os_exit(0)
}
