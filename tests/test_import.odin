package main

import "test_import_helper"

main :: proc() {
    result := test_import_helper.helper_constant
    _ = result
}
