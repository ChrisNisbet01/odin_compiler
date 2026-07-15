package main

import "core:os"

main :: proc() {
    v: #simd [4]f32

    // Single-component swizzle
    x := v.x
    y := v.y
    z := v.z
    w := v.w
    if x != 0.0 { os.exit(1) }
    if y != 0.0 { os.exit(2) }
    if z != 0.0 { os.exit(3) }
    if w != 0.0 { os.exit(4) }

    // Multi-component swizzle
    xy := v.xy
    xyz := v.xyz
    xyzw := v.xyzw
    if xy.x != 0.0 { os.exit(5) }
    if xyzw.w != 0.0 { os.exit(6) }

    // Reversed swizzle
    zyx := v.zyx
    wwww := v.wwww
    if zyx.x != 0.0 { os.exit(7) }
    if wwww.x != 0.0 { os.exit(8) }

    // RGBA set
    r := v.r
    g := v.g
    b := v.b
    a := v.a
    if r != 0.0 { os.exit(9) }
    if a != 0.0 { os.exit(10) }

    // Multi-component RGBA
    rg := v.rg
    ba := v.ba
    if rg.x != 0.0 { os.exit(11) }
    if ba.y != 0.0 { os.exit(12) }

    os.exit(0)
}
