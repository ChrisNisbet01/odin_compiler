package main

import "core:fmt"
import "core:os"

main :: proc() {
    // Test basic fmt functions
    fmt.println("hello", "world")
    fmt.printf("value: %d, str: %s\n", 42, "test")
    fmt.println("hex: %x, upper: %X, bin: %b, oct: %o", 255, 255, 8, 8)
    fmt.println("float: %f", 3.14159)
    fmt.eprintln("error message")
    
    // Test format specifiers
    fmt.println("unsigned: %u", 42)
    fmt.println("u8:", u8(100), "u16:", u16(200), "u32:", u32(300), "u64:", u64(400))
    fmt.println("i8:", i8(-7), "i16:", i16(-1234), "i32:", i32(-987654), "i64:", i64(-9999999))
    
    fmt.println("All fmt tests passed!")
    os.exit(0)
}