package main

import "test_import_helper"

main :: proc() {
    f := test_import_helper.helper_func
    _ = f
}
