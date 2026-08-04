package main

import "core:os"
import "core:fmt"
import "core:math/linalg"

main :: proc() {
    result := 0

    // Test 1: 2x2 int transpose
    m1: matrix[2,2]int
    m1[0,0] = 1; m1[0,1] = 2
    m1[1,0] = 3; m1[1,1] = 4

    t1 := linalg.transpose(m1)

    if t1[0,0] != 1 { fmt.println("FAIL t1[0,0]=", t1[0,0]); result = 1 }
    if t1[0,1] != 3 { fmt.println("FAIL t1[0,1]=", t1[0,1]); result = 1 }
    if t1[1,0] != 2 { fmt.println("FAIL t1[1,0]=", t1[1,0]); result = 1 }
    if t1[1,1] != 4 { fmt.println("FAIL t1[1,1]=", t1[1,1]); result = 1 }

    // Test 2: 3x3 int transpose
    m2: matrix[3,3]int
    m2[0,0] = 1; m2[0,1] = 2; m2[0,2] = 3
    m2[1,0] = 4; m2[1,1] = 5; m2[1,2] = 6
    m2[2,0] = 7; m2[2,1] = 8; m2[2,2] = 9

    t2 := linalg.transpose(m2)

    if t2[0,0] != 1 { fmt.println("FAIL t2[0,0]=", t2[0,0]); result = 1 }
    if t2[0,1] != 4 { fmt.println("FAIL t2[0,1]=", t2[0,1]); result = 1 }
    if t2[0,2] != 7 { fmt.println("FAIL t2[0,2]=", t2[0,2]); result = 1 }
    if t2[1,0] != 2 { fmt.println("FAIL t2[1,0]=", t2[1,0]); result = 1 }
    if t2[1,1] != 5 { fmt.println("FAIL t2[1,1]=", t2[1,1]); result = 1 }
    if t2[1,2] != 8 { fmt.println("FAIL t2[1,2]=", t2[1,2]); result = 1 }
    if t2[2,0] != 3 { fmt.println("FAIL t2[2,0]=", t2[2,0]); result = 1 }
    if t2[2,1] != 6 { fmt.println("FAIL t2[2,1]=", t2[2,1]); result = 1 }
    if t2[2,2] != 9 { fmt.println("FAIL t2[2,2]=", t2[2,2]); result = 1 }

    // Test 3: 2x3 (non-square) transpose — result is 3x2
    m3: matrix[2,3]int
    m3[0,0] = 10; m3[0,1] = 20; m3[0,2] = 30
    m3[1,0] = 40; m3[1,1] = 50; m3[1,2] = 60

    t3 := linalg.transpose(m3)

    if t3[0,0] != 10 { fmt.println("FAIL t3[0,0]=", t3[0,0]); result = 1 }
    if t3[0,1] != 40 { fmt.println("FAIL t3[0,1]=", t3[0,1]); result = 1 }
    if t3[1,0] != 20 { fmt.println("FAIL t3[1,0]=", t3[1,0]); result = 1 }
    if t3[1,1] != 50 { fmt.println("FAIL t3[1,1]=", t3[1,1]); result = 1 }
    if t3[2,0] != 30 { fmt.println("FAIL t3[2,0]=", t3[2,0]); result = 1 }
    if t3[2,1] != 60 { fmt.println("FAIL t3[2,1]=", t3[2,1]); result = 1 }

    // Test 4: f64 transpose
    m4: matrix[2,2]f64
    m4[0,0] = 1.5; m4[0,1] = 2.5
    m4[1,0] = 3.5; m4[1,1] = 4.5

    t4 := linalg.transpose(m4)

    if t4[0,0] != 1.5 { fmt.println("FAIL t4[0,0]=", t4[0,0]); result = 1 }
    if t4[0,1] != 3.5 { fmt.println("FAIL t4[0,1]=", t4[0,1]); result = 1 }
    if t4[1,0] != 2.5 { fmt.println("FAIL t4[1,0]=", t4[1,0]); result = 1 }
    if t4[1,1] != 4.5 { fmt.println("FAIL t4[1,1]=", t4[1,1]); result = 1 }

    // Test 5: transpose used in expression
    m5: matrix[2,2]int
    m5[0,0] = 100; m5[0,1] = 200
    m5[1,0] = 300; m5[1,1] = 400

    sum := linalg.transpose(m5)[0,0] + linalg.transpose(m5)[1,1]
    if sum != 500 { fmt.println("FAIL sum=", sum); result = 1 }

    if result == 0 {
        fmt.println("ALL transpose tests passed")
    }
    os.exit(result)
}
