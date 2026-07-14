package main
import "core:os"

main :: proc() {
    arr: [4]int
    arr[0] = 1
    arr[1] = 2
    arr[2] = 3
    arr[3] = 4
    #no_bounds_check
    // Without #no_bounds_check, this OOB access would trap.
    // With #no_bounds_check, it reads garbage but doesn't trap.
    _ = arr[100]
    os.exit(0)
}
