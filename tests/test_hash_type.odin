package main
import "core:os"

main :: proc() {
    Fn :: #type proc(x: int) -> int
    double :: proc(x: int) -> int { return x * 2 }
    f: Fn = double
    os.exit(f(21) - 42)
}
