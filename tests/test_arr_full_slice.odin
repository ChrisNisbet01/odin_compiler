package main
import "core:os"

main :: proc() {
    arr: [5]int
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    arr[3] = 40
    arr[4] = 50

    // Full-slice syntax arr[:]
    s: []int = arr[:]
    if s[0] != 10 {
        os.exit(1)
    }
    if s[4] != 50 {
        os.exit(2)
    }

    // Using := shorthand
    s2 := arr[:]
    if s2[2] != 30 {
        os.exit(3)
    }

    os.exit(0)
}
