package test_poly_cross_pkg_helper

// A simple poly identity proc (no where clause)
identity :: proc(x: $T) -> T {
    return x
}

// A poly proc with where clause
identity_int_only :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

// A poly proc with size_of-based where clause
sum_same_size :: proc(a: $T, b: $T) -> T where size_of(T) == size_of(int) {
    return a + b
}

// A poly proc with two type params
add :: proc(a: $T, b: $T) -> T {
    return a + b
}

// Non-poly proc for bundle testing (always wins for int via exact match)
identity_int :: proc(x: int) -> int {
    return x + 100
}

// Overload bundle of one poly + one non-poly
mixed_bundle :: proc{identity_int, identity}

// Non-poly proc for baseline
helper_int :: proc(x: int) -> int {
    return x + 1
}


