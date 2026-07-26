package main

import "core:fmt"
import "core:strings"
import "core:os"

test_sbprint_basic :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprint(&b, "hello")
    if n != 5 { return false }
    if len(strings.to_string(b)) != 5 { return false }
    return true
}

test_sbprint_multi :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprint(&b, "hello", " ", "world")
    if n != 13 { return false }
    if len(strings.to_string(b)) != 13 { return false }
    return true
}

test_sbprint_int :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprint(&b, "val=", 42)
    if n != 7 { return false }
    if len(strings.to_string(b)) != 7 { return false }
    return true
}

test_sbprint_empty :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprint(&b)
    if n != 0 { return false }
    if len(strings.to_string(b)) != 0 { return false }
    return true
}

test_sbprint_accumulate :: proc() -> bool {
    b := strings.builder_make(64)
    fmt.sbprint(&b, "aaa")
    fmt.sbprint(&b, "bbb")
    fmt.sbprint(&b, "ccc")
    s := strings.to_string(b)
    if len(s) != 9 { return false }
    return true
}

test_sbprintf_str :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "hello %s", "world")
    if n != 11 { return false }
    if len(strings.to_string(b)) != 11 { return false }
    return true
}

test_sbprintf_int :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "val=%d", 42)
    if n != 6 { return false }
    if len(strings.to_string(b)) != 6 { return false }
    return true
}

test_sbprintf_percent :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "100%%")
    if n != 4 { return false }
    if len(strings.to_string(b)) != 4 { return false }
    return true
}

test_sbprintln :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintln(&b, "hello")
    if n != 6 { return false }
    if len(strings.to_string(b)) != 6 { return false }
    return true
}

test_sbprintln_multi :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintln(&b, "a", "b")
    if n != 4 { return false }
    if len(strings.to_string(b)) != 4 { return false }
    return true
}

test_sbprintfln :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintfln(&b, "hello %s", "world")
    if n != 12 { return false }
    if len(strings.to_string(b)) != 12 { return false }
    return true
}

test_sbprintfln_int :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintfln(&b, "count=%d", 100)
    if n != 10 { return false }
    if len(strings.to_string(b)) != 10 { return false }
    return true
}

test_sbprintf_binary :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "%b", 10)
    if n != 4 { return false }
    if len(strings.to_string(b)) != 4 { return false }
    return true
}

test_sbprintf_octal :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "%o", 8)
    if n != 2 { return false }
    if len(strings.to_string(b)) != 2 { return false }
    return true
}

test_sbprintf_hex_upper :: proc() -> bool {
    b := strings.builder_make(64)
    n := fmt.sbprintf(&b, "%X", 255)
    if n != 2 { return false }
    if len(strings.to_string(b)) != 2 { return false }
    return true
}

main :: proc() {
    result := 0
    if !test_sbprint_basic()      { result = 1 }
    if !test_sbprint_multi()      { result = 2 }
    if !test_sbprint_int()        { result = 3 }
    if !test_sbprint_empty()      { result = 4 }
    if !test_sbprint_accumulate() { result = 5 }
    if !test_sbprintf_str()       { result = 6 }
    if !test_sbprintf_int()       { result = 7 }
    if !test_sbprintf_percent()   { result = 8 }
    if !test_sbprintln()          { result = 9 }
    if !test_sbprintln_multi()    { result = 10 }
    if !test_sbprintfln()         { result = 11 }
    if !test_sbprintfln_int()     { result = 12 }
    if !test_sbprintf_binary()    { result = 13 }
    if !test_sbprintf_octal()     { result = 14 }
    if !test_sbprintf_hex_upper() { result = 15 }
    os.exit(result)
}
