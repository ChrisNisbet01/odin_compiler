package test

import "core:os"
import "core:fmt"

// --- Poly-only overload bundle with where clause filtering ---

identity_int :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

identity_f64 :: proc(x: $T) -> T where typeid_of(T) == typeid_of(f64) {
    return x + x
}

dispatch :: proc{identity_int, identity_f64}

// --- Where clause with size_of ---

same_size :: proc(a: $A, b: $B) -> bool where size_of(A) == size_of(B) {
    return true
}

diff_size :: proc(a: $A, b: $B) -> bool where size_of(A) != size_of(B) {
    return false
}

size_dispatch :: proc{same_size, diff_size}

// --- Where clause with logical OR (multi-type match) ---

int_or_f64 :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) || typeid_of(T) == typeid_of(f64) {
    return x
}

main :: proc() {
    // Test 1: dispatch(42) → identity_int matches (typeid_of(int) == typeid_of(int))
    r1 := dispatch(42)
    fmt.printf("r1: %d\n", r1)
    if r1 != 42 do os.exit(1)

    // Test 2: dispatch(3.14) → identity_f64 matches (typeid_of(f64) == typeid_of(f64))
    r2 := dispatch(3.14)
    fmt.printf("r2: %f\n", r2)
    if r2 != 6.28 do os.exit(2)

    // Test 3: size_dispatch(1, 2) → same_size matches (size_of(int) == size_of(int))
    r3 := size_dispatch(1, 2)
    fmt.printf("r3: %d\n", r3)
    if r3 != true do os.exit(3)

    // Test 4: or_dispatch(42) → int_or_f64 matches (int matches "int || f64")
    r4 := int_or_f64(42)
    fmt.printf("r4: %d\n", r4)
    if r4 != 42 do os.exit(4)

    // Test 5: or_dispatch(3.14) → int_or_f64 matches (f64 matches "int || f64")
    r5 := int_or_f64(3.14)
    fmt.printf("r5: %f\n", r5)
    if r5 != 3.14 do os.exit(5)

    fmt.printf("All overload where-clause tests passed\n")
    os.exit(0)
}
