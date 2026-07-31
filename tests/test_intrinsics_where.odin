package main

import "core:os"

@private IS_NUMERIC :: intrinsics.type_is_numeric
@private IS_FLOAT   :: intrinsics.type_is_float
@private IS_ARRAY   :: intrinsics.type_is_array

dot :: proc(a: $T, b: $T) -> T
    where IS_NUMERIC(T), !IS_ARRAY(T)
{
    return a * b
}

fdot :: proc(a: $T, b: $T) -> T
    where IS_FLOAT(T)
{
    return a + b
}

main :: proc() {
    x: int = dot(2, 3)
    if x != 6 { os.exit(1) }

    y: f64 = dot(2.5, 4.0)
    if y != 10.0 { os.exit(2) }

    z: f64 = fdot(1.5, 2.0)
    if z != 3.5 { os.exit(3) }

    os.exit(0)
}
