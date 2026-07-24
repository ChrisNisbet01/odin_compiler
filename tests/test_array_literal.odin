package main

import "core:os"

main :: proc() {
    result := 0

    // 1. Basic array literal
    arr1 := [3]int{10, 20, 30}
    if arr1[0] != 10 { result = result + 1 }
    if arr1[1] != 20 { result = result + 2 }
    if arr1[2] != 30 { result = result + 4 }

    // 2. Array literal with different types
    arr2 := [4]f64{1.0, 2.0, 3.0, 4.0}
    if arr2[0] != 1.0 { result = result + 8 }
    if arr2[3] != 4.0 { result = result + 16 }

    // 3. Empty array literal
    arr3 := [0]int{}
    if len(arr3) != 0 { result = result + 32 }

    // 4. Array literal with expressions
    x := 5
    arr4 := [2]int{x, x + 1}
    if arr4[0] != 5 { result = result + 64 }
    if arr4[1] != 6 { result = result + 128 }

    os.exit(result)
}