package main
import "core:os"

main :: proc() {
    x: int = 42
    p: ^int = &x

    // Basic dereference (rvalue)
    y: int = p^
    result: int = 0

    if y != 42 {
        result = 1
    }

    // Dereference and assign through pointer (lvalue)
    p^ = 100

    if x != 100 {
        result = result + 2
    }

    os.exit(result)
}
