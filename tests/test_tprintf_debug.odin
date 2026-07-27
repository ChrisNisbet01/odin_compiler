package main

import "core:fmt"
import "core:strings"
import "core:os"

test_sbprintf_direct :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "val=%d", 42)
    if n != 6 { return false }
    return true
}

test_tprintf_direct :: proc() -> bool {
    fmt.tprintf("val=%d", 42)
    return true
}

main :: proc() {
    result := 0
    if !test_sbprintf_direct() { result = 1 }
    if !test_tprintf_direct()  { result = 2 }
    os.exit(result)
}
