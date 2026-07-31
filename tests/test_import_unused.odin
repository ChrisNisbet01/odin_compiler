package main

import "test_import_helper"

main :: proc() {
    // Import test_import_helper but don't use it.
    // The import should be skipped during codegen.
    os_exit(0)
}