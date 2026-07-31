package main

import "core:os"

// Compile-time type intrinsics used via @private constant aliases.
// These resolve against stubs/base/intrinsics (auto-imported) and can be
// called directly in where clauses.
@private IS_QUATERNION :: intrinsics.type_is_quaternion
@private IS_ARRAY       :: intrinsics.type_is_array
@private IS_FLOAT       :: intrinsics.type_is_float
@private IS_NUMERIC     :: intrinsics.type_is_numeric
@private BASE_TYPE      :: intrinsics.type_base_type
@private ELEM_TYPE      :: intrinsics.type_elem_type

// Scalar dot product: constrained to numeric, non-array types.
scalar_dot :: proc(a, b: $T) -> T
where IS_NUMERIC(T), !IS_ARRAY(T)
{
    return a * b
}

// Vector dot product: matches $T/[N]$E (array OR #simd vector), requires the
// element type E to equal the array/vector element type and be numeric.
// Compile-time `when N == X` selects the branch for the specialized lane count.
vector_dot :: proc(a, b: $T/[$N]$E) -> E
where ELEM_TYPE(T) == E, IS_NUMERIC(E)
{
    when N == 1 {
        return a.x * b.x
    } else when N == 2 {
        return a.x * b.x + a.y * b.y
    } else when N == 3 {
        return a.x * b.x + a.y * b.y + a.z * b.z
    } else when N == 4 {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
    } else {
        c: E
        c = a.x * b.x
        return c
    }
}

main :: proc() {
    // Scalar
    if scalar_dot(3, 4) != 12 {
        os.exit(1)
    }
    if scalar_dot(1.5, 2.0) != 3.0 {
        os.exit(2)
    }

    // #simd [4]f32: 1*4 + 2*3 + 3*2 + 4*1 = 20
    va: #simd [4]f32
    vb: #simd [4]f32
    va[0] = 1.0
    va[1] = 2.0
    va[2] = 3.0
    va[3] = 4.0
    vb[0] = 4.0
    vb[1] = 3.0
    vb[2] = 2.0
    vb[3] = 1.0
    d4 := vector_dot(va, vb)
    if d4 != 20.0 {
        os.exit(3)
    }

    // #simd [2]f32: 1*3 + 2*4 = 11
    va2: #simd [2]f32
    vb2: #simd [2]f32
    va2[0] = 1.0
    va2[1] = 2.0
    vb2[0] = 3.0
    vb2[1] = 4.0
    d2 := vector_dot(va2, vb2)
    if d2 != 11.0 {
        os.exit(4)
    }

    // #simd [1]f32: 5*6 = 30
    va1: #simd [1]f32
    vb1: #simd [1]f32
    va1[0] = 5.0
    vb1[0] = 6.0
    d1 := vector_dot(va1, vb1)
    if d1 != 30.0 {
        os.exit(5)
    }

    os.exit(0)
}
