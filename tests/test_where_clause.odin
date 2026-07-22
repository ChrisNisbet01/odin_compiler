package test

import "core:os"

// Where clause: only accepts int arguments
identity_int :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

// Where clause: only accepts f64 arguments
identity_f64 :: proc(x: $T) -> T where typeid_of(T) == typeid_of(f64) {
    return x
}

// Where clause: size_of constraint
same_size :: proc(a: $A, b: $B) -> bool where size_of(A) == size_of(B) {
    return true
}

main :: proc()
{
    // Test 1: where clause satisfied (int arg to int-constrained proc)
    r1 := identity_int(42)
    if r1 != 42 do os.exit(1)

    // Test 2: where clause satisfied (f64 arg to f64-constrained proc)
    r2 := identity_f64(3.14)
    if r2 != 3.14 do os.exit(2)

    // Test 3: size_of constraint (int == int: 8 bytes == 8 bytes)
    r3 := same_size(1, 2)
    if r3 != true do os.exit(3)

    os.exit(0)
}
