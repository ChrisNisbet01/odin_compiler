package main

// Variadic with fixed params
foo :: proc(a: int, args: ..any) -> int {
    return a
}

// Variadic only (no fixed)
bar :: proc(args: ..any) -> int {
    return 42
}

// Zero variadic args
baz :: proc(a: int, args: ..any) -> int {
    return a + 1
}

main :: proc() -> int {
    result: int = 0
    result += foo(42, 1, 2, 3)          // 42
    result += bar(10, 20, 30)            // 42
    result += baz(0)                     // 1
    result -= 85
    return result
}
