package main

import "core:os"
import "core:fmt"
import "core:math/linalg"

main :: proc() {
    result := 0

    // Test 1: Single-size transpose with same-size result
    m1: matrix[2,2]int
    m1[0,0] = 1; m1[0,1] = 2
    m1[1,0] = 3; m1[1,1] = 4
    
    t1 := linalg.transpose(m1)
    
    fmt.println("t1[0,0]=", t1[0,0], "expected 1")
    fmt.println("t1[0,1]=", t1[0,1], "expected 3")
    fmt.println("t1[1,0]=", t1[1,0], "expected 2")
    fmt.println("t1[1,1]=", t1[1,1], "expected 4")

    os.exit(result)
}
