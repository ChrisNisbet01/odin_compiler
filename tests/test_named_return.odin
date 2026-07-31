package main

import "core:os"

// Named return: single return variable
single_return :: proc(a, b: int) -> (result: int) {
    result = a + b
}

// Named return: explicit return with expression (should use the variable anyway, but this tests both)
explicit_single :: proc(a, b: int) -> (result: int) {
    return a + b
}

// Named return: multiple return variables
multi_return :: proc(a: int) -> (positive: int, negative: int) {
    if a >= 0 {
        positive = a
        negative = 0
    } else {
        positive = 0
        negative = -a
    }
}

main :: proc() {
    // Test single named return
    if single_return(3, 4) != 7 {
        os.exit(1)
    }

    // Test explicit return with named return
    if explicit_single(5, 6) != 11 {
        os.exit(2)
    }

    // Test multi-return
    pos, neg := multi_return(-5)
    if pos != 0 || neg != 5 {
        os.exit(3)
    }

    pos2, neg2 := multi_return(7)
    if pos2 != 7 || neg2 != 0 {
        os.exit(4)
    }

    os.exit(0)
}