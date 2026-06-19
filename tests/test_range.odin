package main

main :: proc() -> int {
    // Test half-open range: 0..<10
    sum1: int = 0
    for i in 0..<10 {
        sum1 = sum1 + i
    }
    // sum1 should be 0+1+...+9 = 45

    // Test inclusive range: 1..5
    sum2: int = 0
    for j in 1..5 {
        sum2 = sum2 + j
    }
    // sum2 should be 1+2+3+4+5 = 15

    // Combined check
    return (sum1 - 45) + (sum2 - 15)
}
