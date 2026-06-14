package main

main :: proc() -> int {
    a: [3]int
    a[0] = 10
    a[1] = 20
    a[2] = 30
    return a[1] - 20
}
