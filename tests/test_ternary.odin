package main

main :: proc() -> int {
    result := 0

    // Test basic ternary: condition is true, pick first branch
    a := 1 == 1 ? 10 : 20
    if a != 10 { result = result + 1 }

    // Test basic ternary: condition is false, pick second branch
    b := 1 == 0 ? 10 : 20
    if b != 20 { result = result + 2 }

    // Test ternary with comparison expressions
    c := (3 > 1) ? 100 : 200
    if c != 100 { result = result + 4 }

    d := (3 < 1) ? 100 : 200
    if d != 200 { result = result + 8 }

    return result
}
