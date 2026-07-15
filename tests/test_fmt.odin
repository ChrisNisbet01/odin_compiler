package main

import "core:fmt"
import "core:os"

main :: proc() {
    fmt.printf("int: %d\n", 42)
    fmt.printf("hex: %x\n", 255)
    fmt.printf("hex: %x\n", 3735928559)
    fmt.printf("str: %s\n", "hello")
    fmt.printf("pct: %%\n")
    fmt.printf("v int: %v\n", 99)
    fmt.printf("v str: %v\n", "world")
    fmt.printf("v u8: %v\n", u8(65))
    fmt.printf("unsigned: %u\n", 42)
    fmt.printf("unsigned: %u\n", u64(999))
    fmt.printf("mixed: %d + %s = %x\n", 42, "test", 255)
    fmt.println("one", 2, "three", 4)
    fmt.println("u8:", u8(100), "u16:", u16(200), "u32:", u32(300), "u64:", u64(400))

    // Regression checks for argument coercion to int_to_string:
    // Each unsigned type passed through fmt must print its decimal value correctly
    // (previously u16(200) was sign-extended to a giant garbage number because the
    // i16 value was passed to an i64 parameter without zext coercion).
    fmt.println("i8:", i8(-7), "i16:", i16(-1234), "i32:", i32(-987654), "i64:", i64(-9999999))
    fmt.println("u8:", u8(255), "u16:", u16(65535), "u32:", u32(4000000000), "u64:", u64(18000000000))

    // Sanity-check int / uintptr /typeid printing (no crash, value present)
    fmt.println("int:", -42, "uintptr:", uintptr(99), "rune:", rune('A'), "byte:", byte(0x42))

    os.exit(0)
}
