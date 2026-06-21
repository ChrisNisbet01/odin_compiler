package main

main :: proc() -> int {
    result: int = 0

    // Test simple bit_field with two fields
    bf: bit_field { a: int | 3, b: int | 5 }

    // Test write and read back
    bf.a = 5  // 3-bit field: max value 7
    if bf.a != 5 {
        result = result + 1
    }

    // Test field b (5 bits, offset 3)
    bf.b = 17  // 5-bit field: max value 31
    if bf.b != 17 {
        result = result + 2
    }

    // Test that writing one field doesn't corrupt the other
    if bf.a != 5 {
        result = result + 4
    }

    // Test bit_field with single field
    bf_single: bit_field { flag: int | 1 }
    bf_single.flag = 1
    if bf_single.flag != 1 {
        result = result + 8
    }

    // Test bit_field with more fields
    bf3: bit_field { x: int | 2, y: int | 3, z: int | 3 }
    bf3.x = 3
    bf3.y = 5
    bf3.z = 6
    if bf3.x != 3 {
        result = result + 16
    }
    if bf3.y != 5 {
        result = result + 32
    }
    if bf3.z != 6 {
        result = result + 64
    }

    // Test overwrite field
    bf3.x = 0
    if bf3.x != 0 {
        result = result + 128
    }

    // Test n-bit field where n = 0 (no fields -- not tested since requires at least 1)
    // Test 64-bit backing (8 fields * 8 bits = 64)
    bf_wide: bit_field { a: int | 8, b: int | 8, c: int | 8, d: int | 8, e: int | 8, f: int | 8, g: int | 8, h: int | 8 }
    bf_wide.a = 255
    bf_wide.b = 128
    bf_wide.c = 64
    bf_wide.d = 32
    bf_wide.e = 16
    bf_wide.f = 8
    bf_wide.g = 4
    bf_wide.h = 2
    if bf_wide.a != 255 { result = result + 256 }
    if bf_wide.c != 64 { result = result + 512 }

    return result
}
