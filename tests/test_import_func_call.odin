package main

import "test_import_helper"

main :: proc() -> int {
    result := test_import_helper.helper_func(41)
    return result - 42
}
