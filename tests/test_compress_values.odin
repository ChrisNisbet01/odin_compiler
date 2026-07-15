package test_compress_values
import "core:os"

Vec3 :: struct { x: int; y: int; z: int }

main :: proc() {
    // Test 1: compress struct from individual values
    v := compress_values(Vec3, 10, 20, 30)
    if v.x != 10 do os.exit(1)
    if v.y != 20 do os.exit(2)
    if v.z != 30 do os.exit(3)

    // Test 2: compress array from individual values
    arr := compress_values([3]int, 100, 200, 300)
    if arr[0] != 100 do os.exit(4)
    if arr[1] != 200 do os.exit(5)
    if arr[2] != 300 do os.exit(6)

    // Test 3: compressed values can be used in expressions
    sum := v.x + v.y + v.z
    if sum != 60 do os.exit(7)

    // Test 4: verify sum of array elements
    arr_sum := arr[0] + arr[1] + arr[2]
    if arr_sum != 600 do os.exit(8)

    os.exit(0)
}
