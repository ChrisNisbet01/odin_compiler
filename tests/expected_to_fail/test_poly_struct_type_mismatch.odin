package main

import "core:os"

Box :: struct($T: typeid) {
    val: T
}

main :: proc() {
    // Mismatch: Box expects 1 type arg, given 2
    b: Box(int, f64)
    b.val = 42
    os.exit(0)
}
