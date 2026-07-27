package main

import "core:os"

main :: proc() {
    result: int = 0

    // Equal strings
    if "hello" == "hello" {
        result = result + 0
    } else {
        result = result + 1
    }

    // Different strings (same length)
    if "hello" == "world" {
        result = result + 2
    }

    // Different lengths
    if "abc" == "abcd" {
        result = result + 4
    }

    // String variable == string literal
    s := "test"
    if s == "test" {
        result = result + 0
    } else {
        result = result + 8
    }

    // String variable != different string literal
    if s != "other" {
        result = result + 0
    } else {
        result = result + 16
    }

    // String variable == different string variable
    s2 := "other"
    if s == s2 {
        result = result + 32
    }

    // String variable != same string variable
    s3 := "test"
    if s != s3 {
        result = result + 64
    }

    // Empty string == empty string
    empty := ""
    if empty == "" {
        result = result + 0
    } else {
        result = result + 128
    }

    // Single char strings
    if "a" == "a" {
        result = result + 0
    } else {
        result = result + 256
    }

    os.exit(result)
}
