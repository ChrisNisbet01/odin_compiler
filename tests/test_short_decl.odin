package main

main :: proc() -> int {
    x: int = 5
    x = 10
    return x - x
}
