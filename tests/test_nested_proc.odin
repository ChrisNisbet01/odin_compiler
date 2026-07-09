package main
import "core:os"

main :: proc() {
    // Test 1: basic nested proc
    add :: proc(a: int, b: int) -> int {
        return a + b
    }
    r1 := add(3, 4)

    // Test 2: multiple nested procs at same level
    sub :: proc(a: int, b: int) -> int {
        return a - b
    }
    r2 := sub(10, 3)

    // Test 3: nested proc calling other nested procs
    combine :: proc(x: int, y: int) -> int {
        return add(x, y) + sub(x, y)
    }
    r3 := combine(10, 4)

    // Test 4: nested proc as higher-order function argument
    apply :: proc(f: proc(a: int, b: int) -> int, a: int, b: int) -> int {
        return f(a, b)
    }
    r4 := apply(add, 5, 6)

    // Test 5: store nested proc in variable and call
    f := add
    r5 := f(7, 8)

    // Total: r1=7, r2=7, r3=20, r4=11, r5=15
    // Expected: 60
    // Return 0 on success, non-zero on failure
    os.exit((r1 + r2 + r3 + r4 + r5) - 60)
}
