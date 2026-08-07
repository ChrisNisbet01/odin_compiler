package test

import "core:os"
import "core:fmt"

main :: proc() {
    // Test basic array printing
    arr: [3]int
    fmt.println(arr)  // Should print [0, 0, 0]
    for i in 0..<3 {
        if array_element(arr, i).(int) != 0 { os.exit(1) }
    }

    // Test initialized array
    arr2 := [3]int{1, 2, 3}
    fmt.println(arr2)  // Should print [1, 2, 3]
    for i in 0..<3 {
        expected := i + 1
        if array_element(arr2, i).(int) != expected { os.exit(2) }
    }

    // Test nested array (recursive element type resolution)
    nest: [2][2]int
    nest[0][1] = 7
    nest[1][0] = 9
    fmt.println(nest)  // Should print [[0, 7], [9, 0]]
    inner := array_element(nest, 1)
    if array_element(inner, 0).(int) != 9 { os.exit(3) }

    // Test matrix (column-major by default)
    m: matrix[2,3]int
    fmt.println(m)  // Should print [[0, 0, 0], [0, 0, 0]]
    if matrix_element(m, 1, 2).(int) != 0 { os.exit(4) }

    // Test initialized matrix
    m2 := matrix[2,3]int{1, 2, 3, 4, 5, 6}
    fmt.println(m2)  // Should print [[1, 2, 3], [4, 5, 6]]
    if matrix_element(m2, 0, 0).(int) != 1 { os.exit(5) }
    if matrix_element(m2, 0, 1).(int) != 2 { os.exit(6) }
    if matrix_element(m2, 0, 2).(int) != 3 { os.exit(7) }
    if matrix_element(m2, 1, 0).(int) != 4 { os.exit(8) }
    if matrix_element(m2, 1, 1).(int) != 5 { os.exit(9) }
    if matrix_element(m2, 1, 2).(int) != 6 { os.exit(10) }

    // Test vector
    v: #simd [4]f32
    v = [4]f32{1.0, 2.0, 3.0, 4.0}
    fmt.println(v)  // Should print [1.000000, 2.000000, 3.000000, 4.000000]
    for i in 0..<4 {
        expected := f32(i + 1)
        if array_element(v, i).(f32) != expected { os.exit(11) }
    }

    os.exit(0)
}
