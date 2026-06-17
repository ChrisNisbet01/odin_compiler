package main

main :: proc() -> int {
    // Test 1: if-block scoping
    x: int = 1
    if x == 1 {
        y: int = 10
        x = y
    }
    // y not accessible here
    if x != 10 { return 1 }

    // Test 2: else-block scoping
    if x == 99 {
        z: int = 5
    } else {
        z: int = 20
        x = z
    }
    if x != 20 { return 2 }

    // Test 3: for-loop body scoping
    sum: int = 0
    i: int = 0
    for i < 3 {
        tmp: int = i + 1
        sum += tmp
        i += 1
    }
    // tmp not accessible here
    if sum != 6 { return 3 }

    // Test 4: switch-case scoping
    v: int = 2
    switch v {
    case 1:
        w: int = 100
        v = w
    case 2:
        w: int = 200
        v = w
    }
    if v != 200 { return 4 }

    // Test 5: default case scoping
    d: int = 5
    switch d {
    case 1:
        e: int = 10
    case:
        e: int = 20
        d = e
    }
    if d != 20 { return 5 }

    // Test 6: shadowing via if-block
    a: int = 1
    if a == 1 {
        a: int = 99
        if a != 99 { return 6 }
    }
    if a != 1 { return 7 }

    return 0
}
