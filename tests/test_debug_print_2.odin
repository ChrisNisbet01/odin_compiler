package test

import "core:os"
import "core:fmt"

main :: proc() {
    // Test basic types (these should work)
    fmt.println(42)
    fmt.println(3.14)
    fmt.println("hello")
    
    // Test zero-initialized array (will print as zeros - placeholder)
    arr: [3]int
    fmt.println(arr)
    
    // Test that program at least compiles and runs
    os.exit(0)
}