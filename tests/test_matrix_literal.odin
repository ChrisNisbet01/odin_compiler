package main

import "core:os"
import "core:fmt"

main :: proc() {
    result := 0

    // Test 1: Basic 2x2 int matrix literal
    m1 := matrix[2,2]int{1, 2, 3, 4}
    if m1[0,0] != 1 { fmt.println("FAIL m1[0,0]=", m1[0,0]); result = 1 }
    if m1[0,1] != 2 { fmt.println("FAIL m1[0,1]=", m1[0,1]); result = 1 }
    if m1[1,0] != 3 { fmt.println("FAIL m1[1,0]=", m1[1,0]); result = 1 }
    if m1[1,1] != 4 { fmt.println("FAIL m1[1,1]=", m1[1,1]); result = 1 }

    // Test 2: 3x3 identity-like matrix literal
    m2 := matrix[3,3]int{1, 0, 0, 0, 2, 0, 0, 0, 3}
    if m2[0,0] != 1 { fmt.println("FAIL m2[0,0]=", m2[0,0]); result = 1 }
    if m2[1,1] != 2 { fmt.println("FAIL m2[1,1]=", m2[1,1]); result = 1 }
    if m2[2,2] != 3 { fmt.println("FAIL m2[2,2]=", m2[2,2]); result = 1 }
    if m2[0,1] != 0 { fmt.println("FAIL m2[0,1]=", m2[0,1]); result = 1 }
    if m2[2,0] != 0 { fmt.println("FAIL m2[2,0]=", m2[2,0]); result = 1 }

    // Test 3: f64 matrix literal
    m3 := matrix[2,2]f64{1.5, 2.5, 3.5, 4.5}
    if m3[0,0] != 1.5 { fmt.println("FAIL m3[0,0]=", m3[0,0]); result = 1 }
    if m3[0,1] != 2.5 { fmt.println("FAIL m3[0,1]=", m3[0,1]); result = 1 }
    if m3[1,0] != 3.5 { fmt.println("FAIL m3[1,0]=", m3[1,0]); result = 1 }
    if m3[1,1] != 4.5 { fmt.println("FAIL m3[1,1]=", m3[1,1]); result = 1 }

    // Test 4: Non-square matrix (2x3)
    m4 := matrix[2,3]int{10, 20, 30, 40, 50, 60}
    if m4[0,0] != 10 { fmt.println("FAIL m4[0,0]=", m4[0,0]); result = 1 }
    if m4[0,2] != 30 { fmt.println("FAIL m4[0,2]=", m4[0,2]); result = 1 }
    if m4[1,0] != 40 { fmt.println("FAIL m4[1,0]=", m4[1,0]); result = 1 }
    if m4[1,2] != 60 { fmt.println("FAIL m4[1,2]=", m4[1,2]); result = 1 }

    // Test 5: Matrix literal with expression elements
    x := 10
    m5 := matrix[2,2]int{x, x + 1, x * 2, x * 3}
    if m5[0,0] != 10 { fmt.println("FAIL m5[0,0]=", m5[0,0]); result = 1 }
    if m5[0,1] != 11 { fmt.println("FAIL m5[0,1]=", m5[0,1]); result = 1 }
    if m5[1,0] != 20 { fmt.println("FAIL m5[1,0]=", m5[1,0]); result = 1 }
    if m5[1,1] != 30 { fmt.println("FAIL m5[1,1]=", m5[1,1]); result = 1 }

    // Test 6: Matrix literal used in arithmetic expression
    m6 := matrix[2,2]int{1, 0, 0, 1}  // identity
    sum := m6[0,0] + m6[1,1]
    if sum != 2 { fmt.println("FAIL sum=", sum); result = 1 }

    // Test 7: Matrix literal passed to function
    if test_func(matrix[2,2]int{5, 6, 7, 8}) != 26 { result = 1 }

    if result == 0 {
        fmt.println("ALL matrix literal tests passed")
    }
    os.exit(result)
}

test_func :: proc(m: matrix[2,2]int) -> int {
    return m[0,0] + m[0,1] + m[1,0] + m[1,1]
}
