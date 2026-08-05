package main

import "core:math/linalg"

main :: proc() {
    m: matrix[2,2]int
    m[0,0] = 1
    t := transpose(m)
    _ = t
}
