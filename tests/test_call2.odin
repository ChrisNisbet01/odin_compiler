package main
import "core:os"

zero :: proc() -> int {
    return 0
}

double :: proc(a: int, b: int) -> int {
    return a + b
}

main :: proc() {
    a: int = zero()
    b: int = double(5, 3) - double(2, 1)
    os.exit(b - b)
}
