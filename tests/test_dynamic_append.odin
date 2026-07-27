package main

import "core:os"

main :: proc() {
    // Test 1: direct dynamic array append
    arr: [dynamic]int
    arr = append(arr, 10)
    arr = append(arr, 20)
    arr = append(arr, 30)
    if len(arr) != 3 { os.exit(1) }
    if arr[0] != 10 { os.exit(2) }
    if arr[1] != 20 { os.exit(3) }
    if arr[2] != 30 { os.exit(4) }
    os.exit(0)
}
