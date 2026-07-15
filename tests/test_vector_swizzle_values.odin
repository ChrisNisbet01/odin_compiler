package main

import "core:os"

main :: proc() {
    v: #simd [4]f32

    // Zero-initialized: all elements are 0.0
    x := v.x
    y := v.y
    z := v.z
    w := v.w
    if x != 0.0 { os.exit(1) }
    if y != 0.0 { os.exit(2) }
    if z != 0.0 { os.exit(3) }
    if w != 0.0 { os.exit(4) }

    a0 := v[0]
    a1 := v[1]
    a2 := v[2]
    a3 := v[3]
    if a0 != 0.0 { os.exit(5) }
    if a1 != 0.0 { os.exit(6) }
    if a2 != 0.0 { os.exit(7) }
    if a3 != 0.0 { os.exit(8) }

    xy := v.xy
    if xy.x != 0.0 { os.exit(9) }
    if xy.y != 0.0 { os.exit(10) }

    rg := v.rg
    if rg.x != 0.0 { os.exit(11) }
    if rg.y != 0.0 { os.exit(12) }

    zyx := v.zyx
    if zyx.x != 0.0 { os.exit(13) }
    if zyx.y != 0.0 { os.exit(14) }
    if zyx.z != 0.0 { os.exit(15) }

    os.exit(0)
}
