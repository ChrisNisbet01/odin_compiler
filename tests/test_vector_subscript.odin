package main

import "core:os"

main :: proc() {
    v: #simd [4]f32

    // Zero-initialized: all elements are 0.0
    a0 := v[0]
    a1 := v[1]
    a2 := v[2]
    a3 := v[3]
    if a0 != 0.0 { os.exit(1) }
    if a1 != 0.0 { os.exit(2) }
    if a2 != 0.0 { os.exit(3) }
    if a3 != 0.0 { os.exit(4) }

    os.exit(0)
}
