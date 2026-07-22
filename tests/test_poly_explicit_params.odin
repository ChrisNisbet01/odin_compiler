package test

import "core:os"
import "core:fmt"

// Explicit $T: typeid declaration — calls use shorthand (inferred $T from value args)
identity_explicit :: proc($T: typeid, x: T) -> T {
    return x
}

// Explicit $T: typeid — return type uses T
first_of :: proc($T: typeid, a: T, b: T) -> T {
    return a
}

// $N int param in shorthand form (proven to work)
sum_array :: proc(arr: [$N]int) -> int {
    total := 0
    for i in 0..<N do total += arr[i]
    return total
}

main :: proc() {
    // Test 1: explicit $T: typeid with int arg (infers $T=int)
    r1 := identity_explicit(42)
    fmt.printf("r1: %d\n", r1)
    if r1 != 42 do os.exit(1)

    // Test 2: explicit $T: typeid with f64 arg (infers $T=f64)
    r2 := identity_explicit(3.14)
    fmt.printf("r2: %f\n", r2)
    if r2 != 3.14 do os.exit(2)

    // Test 3: explicit $T: typeid with bool arg
    r3 := identity_explicit(true)
    fmt.printf("r3: %d\n", r3)
    if r3 != true do os.exit(3)

    // Test 4: explicit $T with first_of — verifies $T inferred and return type correct
    r4 := first_of(100, 200)
    fmt.printf("r4: %d\n", r4)
    if r4 != 100 do os.exit(4)

    // Test 5: explicit $T with first_of using f64
    r5 := first_of(1.5, 2.5)
    fmt.printf("r5: %f\n", r5)
    if r5 != 1.5 do os.exit(5)

    // Test 6: $N int param (shorthand, proven to work)
    a: [3]int
    a[0] = 10
    a[1] = 20
    a[2] = 30
    r6 := sum_array(a)
    fmt.printf("r6: %d\n", r6)
    if r6 != 60 do os.exit(6)

    fmt.printf("All explicit poly param tests passed\n")
    os.exit(0)
}
