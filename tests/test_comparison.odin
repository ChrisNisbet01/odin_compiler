package main

main :: proc() -> int {
    x: int = 5
    y: int = 10
    lt: int = x < y
    return lt - lt
}
