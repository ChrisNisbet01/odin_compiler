package main
import "core:os"

import using "test_import_helper"

main :: proc() {
    result := helper_func(41)
    os.exit(result - 42)
}
