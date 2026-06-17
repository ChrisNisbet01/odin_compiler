package main

main :: proc() -> int {
    x: int = 5
    y: int = 10
    if x > 10 {
        y = 100
    } else {
        y = 200
    }
    return y - y
}
