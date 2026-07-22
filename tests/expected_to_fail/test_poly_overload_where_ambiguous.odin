package test

import "core:os"

// Both where clauses are satisfied for int arg — should be ambiguous
identity_int_a :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x
}

identity_int_b :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
    return x + x
}

dispatch_ambiguous :: proc{identity_int_a, identity_int_b}

main :: proc() {
    // Both candidates match int arg — should fail with ambiguity error
    r := dispatch_ambiguous(42)
    os.exit(0)
}
