package main

import using "test_import_helper"

main :: proc() -> int {
    result := helper_func(41)
    return result - 42
}
