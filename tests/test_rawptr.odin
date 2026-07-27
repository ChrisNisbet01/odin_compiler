package main

import "core:os"

main :: proc() {
    result: int = 0

    // Basic rawptr variable declaration
    p: rawptr
    if p == nil {
        result = result + 0
    } else {
        result = result + 1
    }

    // Assign rawptr from address-of
    x: int = 42
    p2: rawptr = &x
    if p2 != nil {
        result = result + 0
    } else {
        result = result + 2
    }

    // rawptr == nil after initialization
    p3: rawptr = nil
    if p3 == nil {
        result = result + 0
    } else {
        result = result + 4
    }

    os.exit(result)
}
