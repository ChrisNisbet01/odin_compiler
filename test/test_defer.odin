package main

main :: proc() -> int {
    result := 0

    // Test 1: Basic defer in a scope
    x := 0
    if true {
        defer x = 1
    }
    if x != 1 {
        result = 1
    }

    // Test 2: Multiple defers in same scope (LIFO)
    y := ""
    if true {
        defer y = y + "A"
        defer y = y + "B"
        y = "C"
    }
    if y != "CBA" {
        result = 2
    }

    // Test 3: Nested defers
    z := ""
    if true {
        defer z = z + "1"
        if true {
            defer z = z + "2"
            z = z + "3"
        }
        z = z + "4"
    }
    if z != "3241" {
        result = 3
    }

    // Test 4: Defer with scope exit (nested)
    a := 0
    if true {
        defer a = 1
        if true {
            defer a = 2
        }
    }
    if a != 1 {
        result = 4
    }

    // Test 5: Break inside loop emits defers
    b := 0
    for b < 10 {
        defer b = 999
        break
    }
    if b != 999 {
        result = 5
    }

    // Test 6: Continue inside loop emits defers
    c := 0
    i := 0
    for i < 5 {
        i = i + 1
        defer c = c + 1
        continue
    }
    if c != 5 {
        result = 6
    }

    // Test 7: Nested defers with break (inner fires first, then outer)
    d := 0
    for d < 10 {
        defer d = 100
        if true {
            defer d = 200
            break
        }
    }
    if d != 100 {
        result = 7
    }

    // Test 8: Break inside for loop body (defer at body scope)
    e := 0
    for e < 10 {
        defer e = 500
        break
    }
    if e != 500 {
        result = 8
    }

    // Test 9: Scope exit defers
    f := 0
    if true {
        defer f = 777
    }
    if f != 777 {
        result = 9
    }

    // Test 10: Multiple defers with break (LIFO on break)
    g := 0
    for g < 10 {
        defer g = g * 10 + 2
        defer g = g * 10 + 1
        g = 0
        break
    }
    if g != 21 {
        result = 10
    }

    return result
}
