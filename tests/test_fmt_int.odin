package main

import "core:fmt"
import "core:os"

main :: proc() {
    fmt.println(42, -7, 0, 100)
    os.exit(0)
}
