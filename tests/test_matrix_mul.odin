package main

import "core:os"

main :: proc() {
    result := 0

    // 1. 2x3 int matrix * 3x2 int matrix
    a: matrix[2,3]int
    a[0][0] = 1; a[0][1] = 2; a[0][2] = 3
    a[1][0] = 4; a[1][1] = 5; a[1][2] = 6
    b: matrix[3,2]int
    b[0][0] = 7; b[0][1] = 8
    b[1][0] = 9; b[1][1] = 10
    b[2][0] = 11; b[2][1] = 12
    c := a * b
    // c should be [2x2]: [1*7+2*9+3*11 = 58, 1*8+2*10+3*12 = 64]
    //                    [4*7+5*9+6*11 = 139, 4*8+5*10+6*12 = 154]
    if c[0][0] != 58 { result = result + 1 }
    if c[0][1] != 64 { result = result + 2 }
    if c[1][0] != 139 { result = result + 4 }
    if c[1][1] != 154 { result = result + 8 }

    // 2. 2x2 int matrix * 2x2 int (square)
    d: matrix[2,2]int
    d[0][0] = 1; d[0][1] = 2
    d[1][0] = 3; d[1][1] = 4
    e: matrix[2,2]int
    e[0][0] = 5; e[0][1] = 6
    e[1][0] = 7; e[1][1] = 8
    f := d * e
    // [1*5+2*7=19, 1*6+2*8=22]
    // [3*5+4*7=43, 3*6+4*8=50]
    if f[0][0] != 19 { result = result + 16 }
    if f[0][1] != 22 { result = result + 32 }
    if f[1][0] != 43 { result = result + 64 }
    if f[1][1] != 50 { result = result + 128 }

    // 3. 2x2 f64 matrix * 2x2 f64 matrix
    g: matrix[2,2]f64
    g[0][0] = 1.0; g[0][1] = 2.0
    g[1][0] = 3.0; g[1][1] = 4.0
    h: matrix[2,2]f64
    h[0][0] = 5.0; h[0][1] = 6.0
    h[1][0] = 7.0; h[1][1] = 8.0
    i := g * h
    if i[0][0] != 19.0 { result = result + 256 }
    if i[0][1] != 22.0 { result = result + 512 }
    if i[1][0] != 43.0 { result = result + 1024 }
    if i[1][1] != 50.0 { result = result + 2048 }

    os.exit(result)
}
