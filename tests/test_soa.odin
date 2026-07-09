package main
import "core:os"

main :: proc() {
    // Test 1: struct #soa with slice-backed fields
    s1: struct #soa { x: int; y: int }
    lx1 := len(s1.x)
    ly1 := len(s1.y)
    // Slices start empty (len == 0)

    // Test 2: #soa[N] with array-backed fields
    s2: #soa[10] struct { a: int; b: int }
    la := len(s2.a)
    lb := len(s2.b)

    // Test 3: Subscript access on array-backed SOA field
    s2.a[0] = 5
    s2.a[1] = 7
    val0 := s2.a[0]
    val1 := s2.a[1]
    sum := val0 + val1

    // Test 4: cap() on array-backed SOA field
    ca := cap(s2.a)

    // Test 5: One-element array-backed SOA
    s3: #soa[1] struct { z: int }
    s3.z[0] = 42
    zv := s3.z[0]

    total := lx1 + ly1 + la + lb + sum + ca + zv
    os.exit(cast(int)(total - (0 + 0 + 10 + 10 + 12 + 10 + 42)))
}
