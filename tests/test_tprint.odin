package main

import "core:fmt"
import "core:os"

test_tprintln_int :: proc() -> bool {
    fmt.tprintln(42)
    return true
}

test_tprintln_str :: proc() -> bool {
    fmt.tprintln("hello")
    return true
}

test_tprintln_multi :: proc() -> bool {
    fmt.tprintln(1, "abc", 3)
    return true
}

test_tprintf_str :: proc() -> bool {
    fmt.tprintf("hello %s", "world")
    return true
}

test_tprintf_int :: proc() -> bool {
    fmt.tprintf("val=%d", 42)
    return true
}

test_tprintfln :: proc() -> bool {
    fmt.tprintfln("count=%d", 100)
    return true
}

test_teprintln :: proc() -> bool {
    fmt.teprintln("stderr", 42)
    return true
}

main :: proc() {
    result := 0
    if !test_tprintln_int()     { result = 1 }
    if !test_tprintln_str()     { result = 2 }
    if !test_tprintln_multi()   { result = 3 }
    if !test_tprintf_str()      { result = 4 }
    if !test_tprintf_int()      { result = 5 }
    if !test_tprintfln()        { result = 6 }
    if !test_teprintln()        { result = 7 }
    os.exit(result)
}
