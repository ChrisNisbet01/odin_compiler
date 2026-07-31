package main

import "core:fmt"
import "core:os"

@private IS_QUATERNION :: intrinsics.type_is_quaternion
@private IS_ARRAY       :: intrinsics.type_is_array
@private IS_FLOAT       :: intrinsics.type_is_float
@private IS_NUMERIC     :: intrinsics.type_is_numeric
@private BASE_TYPE      :: intrinsics.type_base_type
@private ELEM_TYPE      :: intrinsics.type_elem_type

scalar_dot :: proc(a, b: $T) -> T
where IS_NUMERIC(T), !IS_ARRAY(T)
{
    return a * b
}

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
        #unroll for i in 0..<N {
            c += a[i] * b[i]
        }
        return c
    }
}

main :: proc() {
    // Scalar tests
    if scalar_dot(3, 4) != 12 {
        os.exit(1)
    }
    if scalar_dot(1.5, 2.0) != 3.0 {
        os.exit(2)
    }

    // #simd [4]f32: 1*4 + 2*3 + 3*2 + 4*1 = 20
    va: #simd [4]f32 = [4]f32{1.0, 2.0, 3.0, 4.0}
    vb: #simd [4]f32 = [4]f32{4.0, 3.0, 2.0, 1.0}
    d4 := vector_dot(va, vb)
    if d4 != 20.0 {
        os.exit(3)
    }

    // #simd [2]f32: 1*3 + 2*4 = 11
    va2: #simd [2]f32 = [2]f32{1.0, 2.0}
    vb2: #simd [2]f32 = [2]f32{3.0, 4.0}
    d2 := vector_dot(va2, vb2)
    if d2 != 11.0 {
        os.exit(4)
    }

    // #simd [1]f32: 5*6 = 30
    va1: #simd [1]f32 = [1]f32{5.0}
    vb1: #simd [1]f32 = [1]f32{6.0}
    d1 := vector_dot(va1, vb1)
    if d1 != 30.0 {
        os.exit(5)
    }

    // Print result for visual confirmation
    fmt.println(vector_dot(va, vb))

    os.exit(0)
}