package main

import "private_helper"
import "core:os"

main :: proc() {
    result := private_helper.helper_public(41)
    if result != 42 {
        os.exit(1)
    }
    os.exit(0)
}
