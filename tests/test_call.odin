package main
import "core:os"

add :: proc(a: int, b: int) -> int {
    return a + b
}

main :: proc() {
    result: int = add(3, 4)
    os.exit(result - result)
}
