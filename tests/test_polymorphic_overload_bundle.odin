package test

import "core:os"
import "core:fmt"

print_int :: proc(x: int) -> int { return x }

double_poly :: proc(x: $T) -> T { return x + x }

do_op :: proc{print_int, double_poly}

main :: proc() {
    // print_int wins (exact match) → a = 42
    a := do_op(42)
    // print_int fails (float != int), double_poly specializes with $T=float → b = 6.28
    b := do_op(3.14)

    fmt.printf("a: %d, b: %f\n", a, b)

    if a != 42 {
        os.exit(1)
    }
    if b != 6.28 {
        os.exit(2)
    }
    os.exit(0)
}
