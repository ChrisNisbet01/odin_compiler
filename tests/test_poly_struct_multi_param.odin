package main

import "core:os"

// Polymorphic struct with two type parameters
Pair :: struct($A: typeid, $B: typeid) {
    first: A;
    second: B
}

// Polymorphic struct with three type parameters
Triple :: struct($A: typeid, $B: typeid, $C: typeid) {
    a: A;
    b: B;
    c: C
}

main :: proc() {
    result := 0

    // 1. Two-param struct: Pair(int, f64)
    p: Pair(int, f64)
    p.first = 10
    p.second = 3.14
    if p.first != 10 {
        result = result + 1
    }
    if p.second != 3.14 {
        result = result + 2
    }

    // 2. Two-param struct with same types: Pair(int, int)
    p2: Pair(int, int)
    p2.first = 20
    p2.second = 30
    if p2.first != 20 {
        result = result + 4
    }
    if p2.second != 30 {
        result = result + 8
    }

    // 3. Three-param struct: Triple(int, f64, u32)
    t: Triple(int, f64, u32)
    t.a = 1
    t.b = 2.5
    t.c = 99
    if t.a != 1 {
        result = result + 16
    }
    if t.b != 2.5 {
        result = result + 32
    }
    if t.c != 99 {
        result = result + 64
    }

    // 4. Second instantiation of Pair with different types
    p3: Pair(f64, int)
    p3.first = 1.5
    p3.second = 7
    if p3.first != 1.5 {
        result = result + 128
    }
    if p3.second != 7 {
        result = result + 256
    }

    os.exit(result)
}
