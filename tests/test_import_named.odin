package main

import alias "test_import_helper"

main :: proc() {
    result := alias.helper_constant
    _ = result
}
