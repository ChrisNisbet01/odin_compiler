package main

add :: proc(a: int, b: int) -> int {
    return a + b
}

main :: proc() -> int {
    result := add(10, 20)
    return result - 30
}
