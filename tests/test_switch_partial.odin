package main

import "core:os"

main :: proc() {
    x: int = 2
    switch #partial x {
    case 1:
        os.exit(1)
    case 2:
        os.exit(0)  // No default clause needed with #partial
    }
    os.exit(2)
}
