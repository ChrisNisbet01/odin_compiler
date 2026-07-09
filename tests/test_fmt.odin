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
    os.exit(0)
}
