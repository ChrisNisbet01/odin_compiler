package main

main :: proc() -> int {
    result: int = 0
    s := make([]int, 5)
    if len(s) != 5 {
        result = result + 1
    }
    return result
}
