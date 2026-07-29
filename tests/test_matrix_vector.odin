package main

import "core:os"

main :: proc() {
    result := 0

    // Matrix × Vector (matrix * array)
    // m = [[1, 2, 3], [4, 5, 6]] (2x3)
    // v = [7, 8, 9] (3 elements)
    // result = [1*7+2*8+3*9, 4*7+5*8+6*9] = [50, 122]
    m: matrix[2,3]int
    m[0][0] = 1; m[0][1] = 2; m[0][2] = 3
    m[1][0] = 4; m[1][1] = 5; m[1][2] = 6
    v: [3]int
    v[0] = 7; v[1] = 8; v[2] = 9
    r := m * v
    if r[0] != 50 { result = result + 1 }
    if r[1] != 122 { result = result + 2 }

    // Vector × Matrix (array * matrix)
    // v = [7, 8, 9] (3 elements)
    // m = [[1, 2], [3, 4], [5, 6]] (3x2)
    // result = [7*1+8*3+9*5, 7*2+8*4+9*6] = [76, 100]
    m2: matrix[3,2]int
    m2[0][0] = 1; m2[0][1] = 2
    m2[1][0] = 3; m2[1][1] = 4
    m2[2][0] = 5; m2[2][1] = 6
    r2 := v * m2
    if r2[0] != 76 { result = result + 4 }
    if r2[1] != 100 { result = result + 8 }

    // Matrix × Vector (f64)
    mf: matrix[2,3]f64
    mf[0][0] = 1.0; mf[0][1] = 2.0; mf[0][2] = 3.0
    mf[1][0] = 4.0; mf[1][1] = 5.0; mf[1][2] = 6.0
    vf: [3]f64
    vf[0] = 7.0; vf[1] = 8.0; vf[2] = 9.0
    rf := mf * vf
    if rf[0] != 50.0 { result = result + 16 }
    if rf[1] != 122.0 { result = result + 32 }

    os.exit(result)
}