package test_poly_cross_pkg_helper_fail

// Only accepts arguments whose type id matches `int`.
identity_int_only :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

