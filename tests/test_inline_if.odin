package main

import "core:os"

add_one :: proc(x: int) -> int {
    return x + 1
}

main :: proc() {
    // Basic inline if-expression
    a := 1 if true else 2
    b := 1 if false else 2

    // With comparison conditions
    m := 5
    c := 10 if m == 5 else 20
    d := 10 if m == 6 else 20

    // Float branches
    e := 1.0 if m > 0 else 2.0
    f := 1.0 if m < 0 else 2.0

    // else-if chaining (right-associative): a if c1 else (b if c2 else d)
    g := 100 if m == 1 else 200 if m == 5 else 300
    h := 100 if m == 1 else 200 if m == 7 else 300

    // In a return position / procedure result
    r := add_one(0 if m == 5 else 1)

    result: int = 0
    if a == 1 { result += 1 }
    if b == 2 { result += 1 }
    if c == 10 { result += 1 }
    if d == 20 { result += 1 }
    if e == 1.0 { result += 1 }
    if f == 2.0 { result += 1 }
    if g == 200 { result += 1 }
    if h == 300 { result += 1 }
    if r == 1 { result += 1 }

    os.exit(9 - result)
}
