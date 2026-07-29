package main

import "core:os"

main :: proc() {
    result := 0

    // Phase 2: Matrix Multiplication

    // 1. 2x3 int matrix * 3x2 int matrix
    a: matrix[2,3]int
    a[0][0] = 1; a[0][1] = 2; a[0][2] = 3
    a[1][0] = 4; a[1][1] = 5; a[1][2] = 6
    b: matrix[3,2]int
    b[0][0] = 7; b[0][1] = 8
    b[1][0] = 9; b[1][1] = 10
    b[2][0] = 11; b[2][1] = 12
    c := a * b
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

    // Phase 3: Scalar Operations

    // 4. Matrix * Scalar (int)
    j: matrix[2,3]int
    j[0][0] = 1; j[0][1] = 2; j[0][2] = 3
    j[1][0] = 4; j[1][1] = 5; j[1][2] = 6
    k := j * 3
    if k[0][0] != 3 { result = result + 4096 }
    if k[0][1] != 6 { result = result + 8192 }
    if k[0][2] != 9 { result = result + 16384 }
    if k[1][0] != 12 { result = result + 32768 }
    if k[1][1] != 15 { result = result + 65536 }
    if k[1][2] != 18 { result = result + 131072 }

    // 5. Scalar * Matrix (int)
    l := 2 * j
    if l[0][0] != 2 { result = result + 262144 }
    if l[0][1] != 4 { result = result + 524288 }
    if l[0][2] != 6 { result = result + 1048576 }
    if l[1][0] != 8 { result = result + 2097152 }
    if l[1][1] != 10 { result = result + 4194304 }
    if l[1][2] != 12 { result = result + 8388608 }

    // 6. Matrix * Scalar (f64)
    m: matrix[2,2]f64
    m[0][0] = 1.5; m[0][1] = 2.5
    m[1][0] = 3.5; m[1][1] = 4.5
    n := m * 2.0
    if n[0][0] != 3.0 { result = result + 16777216 }
    if n[0][1] != 5.0 { result = result + 33554432 }
    if n[1][0] != 7.0 { result = result + 67108864 }
    if n[1][1] != 9.0 { result = result + 134217728 }

    // 7. Matrix + Matrix (int)
    o: matrix[2,2]int
    o[0][0] = 1; o[0][1] = 2
    o[1][0] = 3; o[1][1] = 4
    p: matrix[2,2]int
    p[0][0] = 5; p[0][1] = 6
    p[1][0] = 7; p[1][1] = 8
    q := o + p
    if q[0][0] != 6 { result = result + 268435456 }
    if q[0][1] != 8 { result = result + 536870912 }
    if q[1][0] != 10 { result = result + 1073741824 }

    // 8. Matrix - Matrix (int)
    r := p - o
    if r[0][0] != 4 { result = result + 1 }  // bit 0
    if r[0][1] != 4 { result = result + 2 }
    if r[1][0] != 4 { result = result + 4 }
    if r[1][1] != 4 { result = result + 8 }

    // 9. Matrix + Matrix (f64)
    s: matrix[2,2]f64
    s[0][0] = 1.0; s[0][1] = 2.0
    s[1][0] = 3.0; s[1][1] = 4.0
    t: matrix[2,2]f64
    t[0][0] = 10.0; t[0][1] = 20.0
    t[1][0] = 30.0; t[1][1] = 40.0
    u := s + t
    if u[0][0] != 11.0 { result = result + 16 }
    if u[0][1] != 22.0 { result = result + 32 }
    if u[1][0] != 33.0 { result = result + 64 }
    if u[1][1] != 44.0 { result = result + 128 }

    // 10. Matrix / Scalar (int)
    v: matrix[2,2]int
    v[0][0] = 10; v[0][1] = 20
    v[1][0] = 30; v[1][1] = 40
    w := v / 10
    if w[0][0] != 1 { result = result + 256 }
    if w[0][1] != 2 { result = result + 512 }
    if w[1][0] != 3 { result = result + 1024 }
    if w[1][1] != 4 { result = result + 2048 }

    os.exit(result)
}
