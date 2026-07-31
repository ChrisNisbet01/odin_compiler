package main

@require import "test_import_helper"

main :: proc() {
    result := test_import_helper.helper_constant
    if result != 42 {
        os_exit(1)
    }
    r2 := test_import_helper.helper_func(41)
    if r2 != 42 {
        os_exit(2)
    }
    os_exit(0)
}