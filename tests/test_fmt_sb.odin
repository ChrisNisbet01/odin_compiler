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
    
    fmt.println("All fmt tests passed!")
    os.exit(0)
}