package main

import "core:fmt"
import "core:strings"
import "core:os"

check_str :: proc(s: string, expected: string) -> bool {
    if len(s) != len(expected) { return false }
    for i in 0..<len(s) {
        if s[i] != expected[i] { return false }
    }
    return true
}

test_sbprint_int :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprint(&b, 42)
    return check_str(strings.to_string(b), "42")
}

test_sbprintf_pct_d :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "%d", 42)
    return check_str(strings.to_string(b), "42")
}

test_sbprintf_pct_s :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "hello %s", "world")
    return check_str(strings.to_string(b), "hello world")
}

test_sbprintf_literal :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "hello world")
    return check_str(strings.to_string(b), "hello world")
}

main :: proc() {
    result := 0
    if !test_sbprint_int()       { result = 1 }
    if !test_sbprintf_pct_d()    { result = 2 }
    if !test_sbprintf_pct_s()    { result = 3 }
    if !test_sbprintf_literal()  { result = 4 }
    os.exit(result)
}
