package main

main :: proc() -> int {
    a: [5]int
    a[0] = 10
    a[1] = 20
    a[2] = 30
    a[3] = 40
    a[4] = 50

    s: []int = a[..]
    result: int = 0

    // Slice subscript read
    if s[0] != 10 {
        result = 1
    }

    if s[2] != 30 {
        result = result + 2
    }

    // Slice subscript write through lvalue
    s[1] = 200
    if a[1] != 200 {
        result = result + 4
    }

    // Slice with both bounds
    t: []int = s[1..3]
    if t[0] != 200 {
        result = result + 8
    }

    if t[1] != 30 {
        result = result + 16
    }

    // Slice with low bound only
    u: []int = s[2..]
    if u[0] != 30 {
        result = result + 32
    }

    // Slice with high bound only
    v: []int = s[..3]
    if v[0] != 10 {
        result = result + 128
    }

    return result
}
