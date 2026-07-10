package main

import "core:os"

main :: proc() {
    // Test standalone #soa without brackets (slice-backed)
    s: #soa struct { x: int; y: int }
    _ = s
    os.exit(0)
}
