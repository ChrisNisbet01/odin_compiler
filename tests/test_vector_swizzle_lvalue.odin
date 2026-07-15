package test_vector_swizzle_lvalue

import "core:os"

main :: proc() {
    // Single-component simple assignment
    v: #simd [4]int
    v.x = 10
    v.y = 20
    v.z = 30
    v.w = 40
    if v.x != 10 { os.exit(1) }
    if v.y != 20 { os.exit(2) }
    if v.z != 30 { os.exit(3) }
    if v.w != 40 { os.exit(4) }

    // Single-component compound assignment
    v.x += 5
    if v.x != 15 { os.exit(5) }

    v.y -= 3
    if v.y != 17 { os.exit(6) }

    // Multi-component simple assignment: xy
    rhs: #simd [2]int
    rhs[0] = 100
    rhs[1] = 200
    a: #simd [4]int
    a.xy = rhs
    if a.x != 100 { os.exit(7) }
    if a.y != 200 { os.exit(8) }
    if a.z != 0   { os.exit(9) }
    if a.w != 0   { os.exit(10) }

    // Multi-component simple assignment: zw
    rhs2: #simd [2]int
    rhs2[0] = 300
    rhs2[1] = 400
    a.zw = rhs2
    if a.z != 300 { os.exit(11) }
    if a.w != 400 { os.exit(12) }
    if a.x != 100 { os.exit(13) }
    if a.y != 200 { os.exit(14) }

    // Non-contiguous swizzle: xz
    c: #simd [4]int
    c[0] = 10
    c[1] = 20
    c[2] = 30
    c[3] = 40
    cxz: #simd [2]int
    cxz[0] = 99
    cxz[1] = 199
    c.xz = cxz
    if c.x != 99  { os.exit(15) }
    if c.y != 20  { os.exit(16) }
    if c.z != 199 { os.exit(17) }
    if c.w != 40  { os.exit(18) }

    // Multi-component compound assignment: xy +=
    d: #simd [4]int
    d[0] = 5
    d[1] = 10
    d[2] = 15
    d[3] = 20
    dplus: #simd [2]int
    dplus[0] = 1
    dplus[1] = 2
    d.xy += dplus
    if d.x != 6  { os.exit(19) }
    if d.y != 12 { os.exit(20) }
    if d.z != 15 { os.exit(21) }
    if d.w != 20 { os.exit(22) }

    // Multi-component compound assignment: zw -=
    dminus: #simd [2]int
    dminus[0] = 3
    dminus[1] = 4
    d.zw -= dminus
    if d.z != 12 { os.exit(23) }
    if d.w != 16 { os.exit(24) }
    if d.x != 6  { os.exit(25) }

    // r/g/b/a set
    e: #simd [4]int
    e.r = 7
    e.g = 8
    e.b = 9
    e.a = 10
    if e.x != 7  { os.exit(26) }
    if e.y != 8  { os.exit(27) }
    if e.z != 9  { os.exit(28) }
    if e.w != 10 { os.exit(29) }

    // r/g compound assignment
    erg: #simd [2]int
    erg[0] = 3
    erg[1] = 4
    e.rg += erg
    if e.x != 10 { os.exit(30) }
    if e.y != 12 { os.exit(31) }

    // Full-lane swizzle: xyzw
    f: #simd [4]int
    f[0] = 1
    f[1] = 2
    f[2] = 3
    f[3] = 4
    fall: #simd [4]int
    fall[0] = 5
    fall[1] = 6
    fall[2] = 7
    fall[3] = 8
    f.xyzw = fall
    if f.x != 5 { os.exit(32) }
    if f.y != 6 { os.exit(33) }
    if f.z != 7 { os.exit(34) }
    if f.w != 8 { os.exit(35) }

    // Partial two-lane overwrite: yw
    g: #simd [4]int
    g[0] = 1
    g[1] = 2
    g[2] = 3
    g[3] = 4
    gyw: #simd [2]int
    gyw[0] = 100
    gyw[1] = 200
    g.yw = gyw
    if g.x != 1   { os.exit(36) }
    if g.y != 100 { os.exit(37) }
    if g.z != 3   { os.exit(38) }
    if g.w != 200 { os.exit(39) }

    os.exit(0)
}
