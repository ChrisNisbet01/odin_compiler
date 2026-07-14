package main
import "core:os"

main :: proc() {
    // Array in-bounds access
    arr: [5]int
    arr[0] = 10
    arr[4] = 50
    if arr[0] != 10 {
        os.exit(1)
    }
    if arr[4] != 50 {
        os.exit(2)
    }

    // Slice in-bounds access
    s: []int = arr[..]
    if s[0] != 10 {
        os.exit(3)
    }
    if s[3] != 0 {
        os.exit(4)
    }
    s[2] = 30
    if s[2] != 30 {
        os.exit(5)
    }

    // String subscript (rvalue)
    str := "hello"
    if str[0] != 'h' {
        os.exit(6)
    }
    if str[4] != 'o' {
        os.exit(7)
    }

    os.exit(0)
}
