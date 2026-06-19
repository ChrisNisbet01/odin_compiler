package main

main :: proc() -> int {
    // Test two-variable for-range: for i, val in range
    sum_i: int = 0
    sum_val: int = 0
    for i, val in 0..<10 {
        sum_i = sum_i + i
        sum_val = sum_val + val
    }
    // Both i and val should be 0..9, so sum should be 45 each
    return (sum_i - 45) + (sum_val - 45)
}
