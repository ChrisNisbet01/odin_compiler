package main
import "core:os"

// Variadic procedure using only fixed parameters
foo :: proc(x: int, ...) -> int {
    return x + 1
}

// Variadic procedure with no fixed params
bar :: proc(...) -> int {
    return 42
}

main :: proc() {
    // Call variadic with extra args
    result := foo(5, 10, 20, 30)
    if result != 6 {
        os.exit(result)
    }

    // Call variadic with no extra args
    result2 := foo(0)
    if result2 != 1 {
        os.exit(result2)
    }

    // Call variadic with no fixed params
    result3 := bar(1, 2, 3)
    if result3 != 42 {
        os.exit(result3)
    }

    // Call variadic with no extra args
    result4 := bar()
    if result4 != 42 {
        os.exit(result4)
    }

    os.exit(0)
}
