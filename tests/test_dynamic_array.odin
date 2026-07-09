package main
import "core:os"

main :: proc() {
    result: int = 0

    // Test make with dynamic array
    da := make([dynamic]int, 5)
    if len(da) != 5 {
        result = result + 1
    }
    if cap(da) != 5 {
        result = result + 2
    }

    // Test subscript write and read
    da[0] = 42
    da[4] = 99
    if da[0] != 42 {
        result = result + 4
    }
    if da[4] != 99 {
        result = result + 8
    }

    // Test len/cap on dynamic array
    if len(da) != 5 {
        result = result + 16
    }
    if cap(da) != 5 {
        result = result + 32
    }

    // Test delete on dynamic array (frees backing memory)
    delete(da)

    // Test make with different size
    da2 := make([dynamic]int, 10)
    if len(da2) != 10 {
        result = result + 64
    }
    if cap(da2) != 10 {
        result = result + 128
    }
    da2[3] = 77
    if da2[3] != 77 {
        result = result + 256
    }
    delete(da2)

    os.exit(result)
}
