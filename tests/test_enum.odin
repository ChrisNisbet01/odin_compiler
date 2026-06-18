package main

main :: proc() -> int {
    c: enum { A, B, C }
    val := cast(int) B
    return val - 1
}
