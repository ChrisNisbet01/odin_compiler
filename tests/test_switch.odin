package main

main :: proc() -> int {
    // Test case matching (first case)
    x: int = 1
    result: int = 0
    switch x {
    case 1:
        result = 10
    case 2:
        result = 20
    }
    if result != 10 { return 1 }

    // Test case matching (second case)
    x = 2
    result = 0
    switch x {
    case 1:
        result = 10
    case 2:
        result = 20
    }
    if result != 20 { return 2 }

    // Test no match falls through
    x = 99
    result = 0
    switch x {
    case 1:
        result = 10
    case 2:
        result = 20
    }
    if result != 0 { return 3 }

    // Test default case (no prior cases match)
    x = 5
    result = 0
    switch x {
    case 1:
        result = 10
    case:
        result = 99
    }
    if result != 99 { return 4 }

    // Test default with no match
    x = 1
    result = 0
    switch x {
    case 2:
        result = 20
    case:
        result = 99
    }
    if result != 99 { return 5 }

    // Test switch with returns
    x = 2
    switch x {
    case 1:
        return 100
    case 2:
        // fall through to normal return
    }

    // Test switch inside for loop (while-style)
    total: int = 0
    i: int = 0
    for i < 3 {
        switch i {
        case 0:
            total += 1
        case 1:
            total += 10
        case:
            total += 100
        }
        i += 1
    }
    if total != 111 { return 6 }

    // Test fallthrough: first case falls through to second case
    x = 1
    result = 0
    switch x {
    case 1:
        result = result + 10
        fallthrough
    case 2:
        result = result + 100
    }
    if result != 110 { return 7 }

    // Test fallthrough: second case falls through to default
    x = 2
    result = 0
    switch x {
    case 1:
        result = 10
    case 2:
        result = result + 20
        fallthrough
    case:
        result = result + 200
    }
    if result != 220 { return 8 }

    // Test fallthrough with no match: no case matched, just default
    x = 99
    result = 0
    switch x {
    case 1:
        result = 10
        fallthrough
    case 2:
        result = 20
    case:
        result = 99
    }
    if result != 99 { return 9 }

    return 0
}
