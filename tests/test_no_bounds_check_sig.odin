package main
import "core:os"

// Test #no_bounds_check placed after the returns clause in a procedure signature
oob_read :: proc(arr: []int, i: int) -> int #no_bounds_check {
    return arr[i]
}

main :: proc() {
    arr: [4]int
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    arr[3] = 40

    s := arr[:]
    // With #no_bounds_check, OOB access should not trap
    r := oob_read(s, 100)
    _ = r

    // Verify normal bounds-checked access still works in another proc
    arr2: [4]int
    arr2[0] = 1
    arr2[1] = 2
    arr2[2] = 3
    arr2[3] = 4
    _ = arr2[0]

    os.exit(0)
}
