package main

import "core:os"
import "core:math/linalg"

main :: proc() {
    result := 0

    // 1. Declare matrix and write/read element using double subscript
    m: matrix[2,3]int
    m[0][0] = 1
    m[0][1] = 2
    m[0][2] = 3
    m[1][0] = 4
    m[1][1] = 5
    m[1][2] = 6
    if m[0][0] != 1 { result = result + 1 }
    if m[0][1] != 2 { result = result + 2 }
    if m[0][2] != 3 { result = result + 4 }
    if m[1][0] != 4 { result = result + 8 }
    if m[1][1] != 5 { result = result + 16 }
    if m[1][2] != 6 { result = result + 32 }

    // 2. Matrix of f64
    n: matrix[2,2]f64
    n[0][0] = 1.5
    n[0][1] = 2.5
    n[1][0] = 3.5
    n[1][1] = 4.5
    if n[0][0] != 1.5 { result = result + 64 }
    if n[1][1] != 4.5 { result = result + 128 }

    // 3. Default zero-initialized matrix elements
    z: matrix[3,4]u8
    if z[0][0] != 0 { result = result + 256 }
    if z[2][3] != 0 { result = result + 512 }

    // 4. Transpose (builtin)
    t := transpose(m)
    if t[0][0] != 1 { result = result + 1024 }
    if t[0][1] != 4 { result = result + 2048 }
    if t[1][0] != 2 { result = result + 4096 }
    if t[1][1] != 5 { result = result + 8192 }
    if t[2][0] != 3 { result = result + 16384 }
    if t[2][1] != 6 { result = result + 32768 }

        // 4. Transpose (builtin)
    t := transpose(m)
    if t[0][0] != 1 { result = result + 1024 }
    if t[0][1] != 4 { result = result + 2048 }
    if t[1][0] != 2 { result = result + 4096 }
    if t[1][1] != 5 { result = result + 8192 }
    if t[2][0] != 3 { result = result + 16384 }
    if t[2][1] != 6 { result = result + 32768 }

    // 5. Determinant of 2x2 matrix
    d := linalg.determinant(n)
    // det = 1.5*4.5 - 2.5*3.5 = 6.75 - 8.75 = -2.0
    if d != -2.0 { result = result + 65536 }

    os.exit(result)
}
