package main

import "private_helper"

main :: proc() -> int {
    result := private_helper.helper_secret(41)
    if result != 42 {
        return 1
    }
    return 0
}
