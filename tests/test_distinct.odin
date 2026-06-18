package main

main :: proc() -> int {
    x: distinct int
    x = 42
    return cast(int) x - 42
}
