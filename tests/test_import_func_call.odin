package main

import "test_import_helper"
import "core:os"

main :: proc() {
    result := test_import_helper.helper_func(41)
    os.exit(result - 42)
}
