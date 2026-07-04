package test_maybe

main :: proc() -> int {
    // Test 1: Maybe(int) = none
    x: Maybe(int) = none
    result := x or_else 42
    if result != 42 {
        return 1
    }

    // Test 2: Maybe(int) = some value
    y: Maybe(int) = 100
    result2 := y or_else 0
    if result2 != 100 {
        return 2
    }

    // Test 3: Maybe(f64)
    z: Maybe(f64) = 3.14
    result3 := z or_else 0.0
    if result3 != 3.14 {
        return 3
    }

    // Test 4: .value member access
    a: Maybe(int) = 42
    v := a.value
    if v != 42 {
        return 4
    }

    // Test 5: Maybe(int) without explicit init (zero = some(0))
    b: Maybe(int)
    result5 := b or_else 99
    if result5 != 0 {
        return 5
    }

    return 0
}
