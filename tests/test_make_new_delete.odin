package main
import "core:os"

main :: proc() {
    result: int = 0

    // Test make with slice
    s := make([]int, 5)
    if len(s) != 5 {
        result = result + 1
    }

    // Test that the slice data is writable
    s[0] = 42
    s[4] = 99
    if s[0] != 42 {
        result = result + 2
    }
    if s[4] != 99 {
        result = result + 4
    }

    // Test new
    p := new(int)
    p^ = 77
    if p^ != 77 {
        result = result + 8
    }

    // Test delete on slice (frees the backing data)
    s2 := make([]int, 3)
    delete(s2)

    // Test delete on pointer from new
    p2 := new(int)
    p2^ = 33
    delete(p2)

    os.exit(result)
}
