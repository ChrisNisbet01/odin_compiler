package test_append

import "core:fmt"
import "core:os"

main :: proc() {
    test_append_single()
    test_append_multiple()
    test_append_from_empty()
    fmt.println("ALL append tests passed")
}

test_append_single :: proc() {
    arr: [dynamic]int
    arr = append(arr, 42)
    if len(arr) != 1 do os.exit(1)
    if arr[0] != 42 do os.exit(2)
    fmt.println("PASS: append single element")
}

test_append_multiple :: proc() {
    arr: [dynamic]int
    arr = append(arr, 10)
    arr = append(arr, 20)
    arr = append(arr, 30)
    if len(arr) != 3 do os.exit(3)
    if arr[0] != 10 do os.exit(4)
    if arr[1] != 20 do os.exit(5)
    if arr[2] != 30 do os.exit(6)
    fmt.println("PASS: append multiple elements")
}

test_append_from_empty :: proc() {
    arr: [dynamic]int
    if len(arr) != 0 do os.exit(7)
    arr = append(arr, 100)
    if len(arr) != 1 do os.exit(8)
    if arr[0] != 100 do os.exit(9)
    fmt.println("PASS: append from empty")
}
