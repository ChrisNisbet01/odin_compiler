package main

main :: proc() -> int {
    result: int = 0

    // Test basic union with different sized fields
    u: union { a: i32; b: i64; c: i32 }

    // Write and read i32 field
    u.a = 42
    if u.a != 42 {
        result = result + 1
    }

    // Write and read i64 field (different size)
    u.b = 1234567890123
    if u.b != 1234567890123 {
        result = result + 2
    }

    // Write to i32 field again, verify it overwrites
    u.a = 99
    if u.a != 99 {
        result = result + 4
    }

    // Write to third i32 field
    u.c = 77
    if u.c != 77 {
        result = result + 8
    }

    // Test union with struct types
    us: union { x: i32; y: i32 }
    us.x = 10
    if us.x != 10 {
        result = result + 16
    }
    us.y = 20
    if us.y != 20 {
        result = result + 32
    }

    // Test union as local copy (read after write chain)
    uv: union { v: i32 }
    uv.v = 5
    uv.v = uv.v + 3
    if uv.v != 8 {
        result = result + 64
    }

    return result
}
