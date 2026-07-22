package test

import "core:os"

// Where clause: only accepts int arguments
identity_int :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

main :: proc()
{
    // This should FAIL: f64 arg to int-constrained proc
    r := identity_int(3.14)
    os.exit(0)
}
