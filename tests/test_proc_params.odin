package main

// Procedure with various parameter types to test parameter type analysis
// All procs declared at top level with different param type patterns

// Basic int param (already tested)
add :: proc(a: int, b: int) -> int {
    return a + b
}

// Bool param
negate :: proc(b: bool) -> int {
    if b {
        return 0
    }
    return 1
}

// Float param
double :: proc(x: f64) -> f64 {
    return x + x
}

// Pointer param
deref_int :: proc(p: ^int) -> int {
    return p^
}

// Mixed param types
mixed :: proc(a: int, b: bool, c: f64) -> int {
    if b {
        return a
    }
    return 0
}

main :: proc() -> int {
    // Test int params
    r1 := add(10, 20)

    // Test bool param
    r2 := negate(false)

    // Test float param
    r3f := double(3.5)

    // Test pointer param  
    x: int = 42
    r4 := deref_int(&x)

    // Test mixed params
    r5 := mixed(7, true, 2.5)

    // Compute final result
    // Verify all values
    // r1=30, r2=1, r3f=7.0, r4=42, r5=7
    // Int sum: 30 + 1 + 42 + 7 = 80
    // Float: r3f should be 7.0 -> cast to int -> 7
    r3 := cast(int) r3f
    result := r1 + r2 + r3 + r4 + r5

    return result - 87
}
