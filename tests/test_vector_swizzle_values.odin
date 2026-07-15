package main

import "core:os"

main :: proc() {
    v: #simd [4]f32

    v[0] = 1.0
    v[1] = 2.0
    v[2] = 3.0
    v[3] = 4.0

    // Single-element swizzle
    x := v.x
    y := v.y
    z := v.z
    w := v.w
    if x != 1.0 { os.exit(1) }
    if y != 2.0 { os.exit(2) }
    if z != 3.0 { os.exit(3) }
    if w != 4.0 { os.exit(4) }

    // Multi-component swizzle
    xy := v.xy
    if xy.x != 1.0 { os.exit(5) }
    if xy.y != 2.0 { os.exit(6) }

    zyx := v.zyx
    if zyx.x != 3.0 { os.exit(7) }
    if zyx.y != 2.0 { os.exit(8) }
    if zyx.z != 1.0 { os.exit(9) }

    wwww := v.wwww
    if wwww.x != 4.0 { os.exit(10) }
    if wwww.y != 4.0 { os.exit(11) }
    if wwww.z != 4.0 { os.exit(12) }
    if wwww.w != 4.0 { os.exit(13) }

    // RGBA set
    r := v.r
    g := v.g
    b := v.b
    a := v.a
    if r != 1.0 { os.exit(14) }
    if g != 2.0 { os.exit(15) }
    if b != 3.0 { os.exit(16) }
    if a != 4.0 { os.exit(17) }

    // Compound assignment
    v[0] += 10.0
    if v.x != 11.0 { os.exit(18) }

    v[1] -= 1.0
    if v.y != 1.0 { os.exit(19) }

    os.exit(0)
}
