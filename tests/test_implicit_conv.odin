package test_implicit_conv

// Test implicit conversion of untyped integer literals to various integer types
// No explicit casts should be needed for literals

// Recursive function with variable declaration init (regression test)
fib :: proc(v: u64) -> u64 {
    if v == 0 || v == 1 {
        return 1
    }
    r: u64 = fib(v - 1) + fib(v - 2)
    return r
}

// Implicit conversion in return statements
returns_u64_from_literal :: proc() -> u64 {
    return 42
}

returns_i64_from_literal :: proc() -> i64 {
    return 42
}

returns_u32_from_literal :: proc() -> u32 {
    return 42
}

returns_i32_from_literal :: proc() -> i32 {
    return 42
}

returns_u16_from_literal :: proc() -> u16 {
    return 42
}

returns_u8_from_literal :: proc() -> u8 {
    return 42
}

returns_f64_from_literal :: proc() -> f64 {
    return 3.14
}

// Implicit conversion in variable declaration init
var_u64_from_literal :: proc() -> u64 {
    x: u64 = 100
    return x
}

var_i64_from_literal :: proc() -> i64 {
    x: i64 = 100
    return x
}

var_u32_from_literal :: proc() -> u32 {
    x: u32 = 100
    return x
}

// Test that u64 + u64 = u64 matches return type
add_u64 :: proc(a: u64, b: u64) -> u64 {
    return a + b
}

main :: proc() {
    // Test recursive fib with variable decl init
    result := fib(10)
    if result != 89 {
        return
    }

    // Test implicit conversions
    if returns_u64_from_literal() != 42 {
        return
    }
    if returns_i64_from_literal() != 42 {
        return
    }
    if returns_u32_from_literal() != 42 {
        return
    }
    if returns_i32_from_literal() != 42 {
        return
    }
    if returns_u16_from_literal() != 42 {
        return
    }
    if returns_u8_from_literal() != 42 {
        return
    }

    // Test variable declarations with implicit conversion
    if var_u64_from_literal() != 100 {
        return
    }
    if var_i64_from_literal() != 100 {
        return
    }
    if var_u32_from_literal() != 100 {
        return
    }

    // Test u64 + u64 = u64
    if add_u64(30, 12) != 42 {
        return
    }
}
