package main
import "core:os"

main :: proc() {
    // Test ternary with variables
    x := 5
    y := 10
    z := x < y ? x : y  // should be 5
    if z != 5 { os.exit(1) }

    // Test ternary with function calls
    get_zero :: proc() -> int { os.exit(0) }
    get_one  :: proc() -> int { os.exit(1) }
    w := get_zero() == 0 ? get_one() : 42  // should be 1
    if w != 1 { os.exit(2) }

    // Test ternary in expression
    result := (1 < 2 ? 10 : 0) + (1 > 2 ? 5 : 3)  // 10 + 3 = 13
    if result != 13 { os.exit(3) }

    os.exit(0)
}