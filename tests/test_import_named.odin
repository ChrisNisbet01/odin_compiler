package main

import alias "test_import_helper"

main :: proc() {
    result := alias.helper_constant
    if result != 42 {
        print_string(2, "FAIL: alias.helper_constant was not 42\n")
        return
    }
    r2 := alias.helper_func(41)
    if r2 != 42 {
        print_string(2, "FAIL: alias.helper_func(41) was not 42\n")
        return
    }
}
