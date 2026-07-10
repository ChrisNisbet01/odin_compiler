package main

import "core:os"

main :: proc() {
    #no_bounds_check
    _ = 42
    os.exit(0)
}
