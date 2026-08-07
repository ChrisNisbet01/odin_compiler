package test

import "core:os"
import "core:fmt"

main :: proc() {
    // Test basic array printing
    arr: [3]int
    fmt.println(arr)  // Should print [0, 0, 0]
    
    // Test initialized array
    arr2 := [3]int{1, 2, 3}
    fmt.println(arr2)  // Should print [1, 2, 3]
    
    // Test matrix
    m: matrix[2,3]int
    fmt.println(m)  // Should print [[0, 0, 0], [0, 0, 0]]
    
    // Test initialized matrix
    m2 := matrix[2,3]int{1, 2, 3, 4, 5, 6}
    fmt.println(m2)  // Should print [[1, 2, 3], [4, 5, 6]]
    
    // Test vector
    v: #simd [4]f32
    v = [4]f32{1.0, 2.0, 3.0, 4.0}
    fmt.println(v)  // Should print [1, 2, 3, 4]
}