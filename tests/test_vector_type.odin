package main

import "core:os"

main :: proc() {
    v: #simd [4]f32

    // Zero-initialized: all elements are 0.0
    if v.x != 0.0 { os.exit(1) }
    if v.y != 0.0 { os.exit(2) }

    s := size_of(#simd [4]f32)
    // #simd [4]f32 = 4 * 4 = 16 bytes
    if s != 16 { os.exit(3) }

    os.exit(0)
}
