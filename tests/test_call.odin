package main

add :: proc(a: int, b: int) -> int {
    return a + b
}

main :: proc() -> int {
    result: int = add(3, 4)
    return result - result
}
