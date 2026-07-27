package main

import "core:fmt"
import "core:strings"
import "core:os"

test_sbprintf_content :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprintf(&b, "val=%d", 42)
    s := strings.to_string(b)
    expected := "val=42"
    if len(s) != len(expected) { return false }
    for i in 0..<len(s) {
        if s[i] != expected[i] { return false }
    }
    return true
}

main :: proc() {
    result := 0
    if !test_sbprintf_content() { result = 1 }
    os.exit(result)
}
