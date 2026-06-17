package main

main :: proc() -> int {
    x: int = 5
    y: int = 10
    if x < 10 {
        y = 20
    }
    return y - 20
}
