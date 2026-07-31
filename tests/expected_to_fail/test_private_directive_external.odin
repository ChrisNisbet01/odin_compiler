package main

import "private_helper"

main :: proc() -> int {
    // Bare @private constant must be blocked cross-package
    result := private_helper.PRIVATE_CONST
    if result != 42 {
        return 1
    }
    return 0
}
