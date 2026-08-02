package test_overload
import "core:os"

// Test 1: Basic overload bundle with int and string variants
foo_int :: proc(x: int) -> int { return x * 2 }
foo_str :: proc(x: string) -> int { return len(x) }
foo :: proc{foo_int, foo_str}

// Test 2: Overload with more than 2 candidates
bar_int :: proc(x: int) -> int { return x + 10 }
bar_f64 :: proc(x: f64) -> int { return 100 }
bar_u8 :: proc(x: u8) -> int { return 200 }
bar :: proc{bar_int, bar_f64, bar_u8}

// Test 3: Multi-arg bundle (bundle declared before its candidates)
multi_op :: proc{
	multi_add,
	multi_mul,
	multi_f64,
}
multi_add :: proc(a: int, b: int) -> int { return a + b }
multi_mul :: proc(a: int, b: int, c: int) -> int { return a * b * c }
multi_f64 :: proc(a: f64, b: f64) -> f64 { return a / b }

main :: proc() {
    // Test 1a: dispatch to foo_int
    r1 := foo(42)
    if r1 != 84 { os.exit(1) }

    // Test 1b: dispatch to foo_str
    r2 := foo("hi")
    if r2 != 2 { os.exit(2) }

    // Test 2a: dispatch to bar_int
    r3 := bar(5)
    if r3 != 15 { os.exit(3) }

    // Test 2b: dispatch to bar_u8
    b: u8 = 7
    r4 := bar(b)
    if r4 != 200 { os.exit(4) }

    // Test 3a: 2-int-arg dispatch to multi_add
    r5 := multi_op(20, 22)
    if r5 != 42 { os.exit(5) }

    // Test 3b: 3-int-arg dispatch to multi_mul
    r6 := multi_op(2, 3, 7)
    if r6 != 42 { os.exit(6) }

    // Test 3c: f64-arg dispatch to multi_f64
    r7 := multi_op(84.0, 2.0)
    if r7 != 42.0 { os.exit(7) }

    os.exit(0)
}
