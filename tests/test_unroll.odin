package main

import "core:os"
import "core:fmt"

main :: proc() {
    // Test 1: Basic unrolled for-range with literal bounds (exclusive)
    // #unroll for i in 0..<4 { sum += i } → 0+1+2+3 = 6
    sum1: i64
    #unroll for i in 0..<4 do sum1 += i
    if sum1 != 6 {
        fmt.println("test 1 failed: sum1 =", sum1, "expected 6")
        os.exit(1)
    }

    // Test 2: Unrolled for-range with larger bounds
    // 0..<5 → 0+1+2+3+4 = 10
    sum2: i64
    #unroll for i in 0..<5 do sum2 += i
    if sum2 != 10 {
        fmt.println("test 2 failed: sum2 =", sum2, "expected 10")
        os.exit(2)
    }

    // Test 3: Nested unrolled loops
    outer_sum: i64
    #unroll for i in 0..<3 {
        inner_sum: i64
        #unroll for j in 0..<3 {
            inner_sum += i * j
        }
        outer_sum += inner_sum
    }
    // i=0: j=0,1,2 → 0
    // i=1: j=0,1,2 → 0+1+2 = 3
    // i=2: j=0,1,2 → 0+2+4 = 6
    // outer_sum = 0+3+6 = 9
    if outer_sum != 9 {
        fmt.println("test 3 failed: outer_sum =", outer_sum, "expected 9")
        os.exit(3)
    }

    // Test 4: Unrolled loop with nested compound statement
    nested: i64
    #unroll for k in 0..<3 {
        nested += k
        nested += k
    }
    // k=0: 0+0=0
    // k=1: 1+1=2
    // k=2: 2+2=4
    // nested = 0+2+4 = 6
    if nested != 6 {
        fmt.println("test 4 failed: nested =", nested, "expected 6")
        os.exit(4)
    }

    // Test 5: Poly unrolled for-range (scalar_dot pattern)
    // Unrolled sum of 0..<N where N=5 (poly $N)
    result5: i64
    N: i64 = 5
    #unroll for i in 0..<N do result5 += i
    if result5 != 10 {
        fmt.println("test 5 failed: result5 =", result5, "expected 10")
        os.exit(5)
    }

    os.exit(0)
}