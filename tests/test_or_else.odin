package main
import "core:os"

helper :: proc() -> int { return 42 }

helper2 :: proc() -> int { return 0 }

main :: proc() {
    result := 0

    // --- or_else tests ---
    a := 1 or_else 99
    if a != 1 { result = result + 1 }

    b := 0 or_else 42
    if b != 42 { result = result + 2 }

    c := (2 or_else 99) + (0 or_else 10)
    if c != 12 { result = result + 4 }

    // --- or_return test 1: non-zero value passes through ---
    d := helper() or_return
    if d != 42 { result = result + 8 }

    // --- or_return test 2: zero value returns early ---
    // helper2() returns 0, so or_return should return from main with 0.
    // If we reach the line below, or_return did NOT fire = bug.
    e := helper2() or_return
    os.exit(result + e + 99)
}
