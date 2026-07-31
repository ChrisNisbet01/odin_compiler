package main

import "private_helper"
import "core:os"

main :: proc() {
    // Bare @private constant used within its own package via a public proc
    r1 := private_helper.uses_private_const()
    if r1 != 42 {
        os.exit(1)
    }

    // Bare @private constant is NOT visible cross-package
    os.exit(0)
}
