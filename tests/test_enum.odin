package main

main :: proc() -> int {
    c: enum { A, B, C }
    return cast(int) B - 1
}
