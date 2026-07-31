package main

import "core:os"
import "core:fmt"

// Poly sum using unrolled for with $N bound at specialization
poly_sum :: proc(arr: [$N]int) -> int
where N > 0
{
    s: int
    #unroll for i in 0..<N do s += arr[i]
    return s
}

main :: proc() {
    // Test 1: Unrolled sum with N=5 (poly specialization)
    arr5: [5]int = [5]int{1, 2, 3, 4, 5}
    result5 := poly_sum(arr5)
    if result5 != 15 {
        fmt.println("test 1 failed: result5 =", result5, "expected 15")
        os.exit(1)
    }

    // Test 2: Unrolled sum with N=3
    arr3: [3]int = [3]int{10, 20, 30}
    result3 := poly_sum(arr3)
    if result3 != 60 {
        fmt.println("test 2 failed: result3 =", result3, "expected 60")
        os.exit(2)
    }

    // Test 3: Unrolled sum with N=1
    arr1: [1]int = [1]int{42}
    result1 := poly_sum(arr1)
    if result1 != 42 {
        fmt.println("test 3 failed: result1 =", result1, "expected 42")
        os.exit(3)
    }

    // Test 4: Regular unrolled for with variable (fallback to runtime, but should still work)
    loop_var_sum: int
    limit: int = 4
    #unroll for i in 0..<limit do loop_var_sum += i
    if loop_var_sum != 6 {
        fmt.println("test 4 failed: loop_var_sum =", loop_var_sum, "expected 6")
        os.exit(4)
    }

    os.exit(0)
}