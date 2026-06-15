package main

main :: proc() -> int {
    add :: proc(a: int, b: int) -> int {
        return a + b
    }
    return add(1, 2) - 3
}
