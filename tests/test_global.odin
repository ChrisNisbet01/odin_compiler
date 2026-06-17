package main

x: int = 42

main :: proc() -> int {
    return x - x
}
