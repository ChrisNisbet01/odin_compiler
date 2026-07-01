package main

import "test_import_cycle_a"

main :: proc() {
    _ = test_import_cycle_a.a_constant
}
