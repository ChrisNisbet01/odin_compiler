package main

main :: proc() -> int {
    sum: int = 0
    i: int = 0
    for i < 10 {
        sum = sum + i
        i = i + 1
    }
    return sum - sum
}
