package test

// Where clause: size_of constraint — mismatched sizes should fail at compile time
same_size :: proc(a: $A, b: $B) -> bool where size_of(A) == size_of(B) {
    return true
}

main :: proc()
{
    // This should fail: int (8 bytes) != f32 (4 bytes)
    r := same_size(1, f32(1.0))
    _ = r
}
