package main

main :: proc() -> int {
    s: struct { x: int; y: int }
    s.x = 10
    s.y = 20
    return s.x + s.y - 30
}
