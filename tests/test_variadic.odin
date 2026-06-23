package main

// Variadic procedure using only fixed parameters
foo :: proc(x: int, ...) -> int {
    return x + 1
}

// Variadic procedure with no fixed params
bar :: proc(...) -> int {
    return 42
}

main :: proc() -> int {
    // Call variadic with extra args
    result := foo(5, 10, 20, 30)
    if result != 6 {
        return result
    }

    // Call variadic with no extra args
    result2 := foo(0)
    if result2 != 1 {
        return result2
    }

    // Call variadic with no fixed params
    result3 := bar(1, 2, 3)
    if result3 != 42 {
        return result3
    }

    // Call variadic with no extra args
    result4 := bar()
    if result4 != 42 {
        return result4
    }

    return 0
}
