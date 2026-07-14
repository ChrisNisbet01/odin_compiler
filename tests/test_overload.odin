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

    os.exit(0)
}
