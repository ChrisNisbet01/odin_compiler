package test_expand_values
import "core:os"

Vec3 :: struct { x: int; y: int; z: int }

sum3 :: proc(a: int, b: int, c: int) -> int {
    return a + b + c
}

main :: proc() {
    // Test 1: expand struct fields as individual args
    v: Vec3
    v.x = 10
    v.y = 20
    v.z = 30
    result := sum3(expand_values(v))
    if result != 60 {
        os.exit(1)
    }

    // Test 2: expand combined with regular args (only expand_values is used here)
    v2: Vec3
    v2.x = 1
    v2.y = 2
    v2.z = 3
    result2 := sum3(expand_values(v2))
    if result2 != 6 {
        os.exit(2)
    }

    os.exit(0)
}
