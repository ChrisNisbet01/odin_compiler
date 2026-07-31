package test

import "core:os"

// Multi-name known-type params
add :: proc(a, b: int) -> int {
    return a + b
}

sub :: proc(a, b: int, c: f64) -> f64 {
    return cast(f64)(a - b) + c
}

// Multi-name poly params
sum :: proc(a, b: $T) -> T {
    return a + b
}

// Multi-name poly + contextless convention + where clause
scalar_dot :: proc "contextless" (a, b: $T) -> T where typeid_of(T) == typeid_of(int) {
    return a * b
}

main :: proc() {
    // Test 1: multi-name known type (int)
    r1 := add(2, 3)
    if r1 != 5 {
        os.exit(1)
    }

    // Test 2: mixed groups (a, b: int, c: f64)
    r2 := sub(10, 4, 0.5)
    if r2 != 6.5 {
        os.exit(2)
    }

    // Test 3: multi-name poly (int)
    r3 := sum(10, 20)
    if r3 != 30 {
        os.exit(3)
    }

    // Test 4: multi-name poly (f64)
    r4 := sum(1.5, 2.5)
    if r4 != 4.0 {
        os.exit(4)
    }

    // Test 5: multi-name poly + contextless + where clause
    r5 := scalar_dot(3, 4)
    if r5 != 12 {
        os.exit(5)
    }

    os.exit(0)
}
