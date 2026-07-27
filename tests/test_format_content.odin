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

test_str_only :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "hello")
    return check_str(strings.to_string(b), "hello")
}

test_str_pct_s :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "hello %s", "world")
    return check_str(strings.to_string(b), "hello world")
}

test_int_pct_d :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "val=%d", 42)
    s := strings.to_string(b)
    return check_str(s, "val=42")
}

main :: proc() {
    result := 0
    if !test_str_only()  { result = 1 }
    if !test_str_pct_s() { result = 2 }
    if !test_int_pct_d() { result = 3 }
    os.exit(result)
}
