package main

import "core:os"
import "core:fmt"

Vec3i :: [3]int

main :: proc() {
    result := 0

    v: Vec3i
    fmt.println(v)

    v2: matrix[1,3]int
    fmt.println(v2)

    os.exit(result)
}
