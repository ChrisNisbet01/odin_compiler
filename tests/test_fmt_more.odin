package main

import "core:fmt"
import "core:os"

main :: proc() {
    // Test printfln
    fmt.printfln("int: %d str: %s", 42, "test")
    fmt.printfln("hex: %x", 255)

    // Test eprintln
    fmt.eprintln("one", 2, "three", 4)

    // Test eprintf
    fmt.eprintf("int: %d str: %s\n", 99, "hello")

    // Test eprintfln
    fmt.eprintfln("val: %v", "world")

    os.exit(0)
}
