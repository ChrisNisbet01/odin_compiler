package test

import "core:os"

add :: proc(x: int, y: int := 10) -> int {
    return x + y;
}

three_args :: proc(a: int, b: int := 20, c: int := 30) -> int {
    return a + b + c;
}

no_defaults_used :: proc(x: int, y: int, z: int) -> int {
    return x + y + z;
}

bool_default :: proc(x: int, flag: bool := true) -> int {
    if flag {
        return x;
    } else {
        return -x;
    }
}

zero_default :: proc(x: int, y: int := 0) -> int {
    return x + y;
}

main :: proc() {
    // Test 1: single default — use default
    r1 := add(5);
    if r1 != 15 {
        os.exit(1);
    }

    // Test 2: single default — override
    r2 := add(5, 3);
    if r2 != 8 {
        os.exit(2);
    }

    // Test 3: multiple defaults — all defaults
    r3 := three_args(1);
    if r3 != 51 {
        os.exit(3);
    }

    // Test 4: multiple defaults — override first default only
    r4 := three_args(1, 100);
    if r4 != 131 {
        os.exit(4);
    }

    // Test 5: multiple defaults — override both defaults
    r5 := three_args(1, 2, 3);
    if r5 != 6 {
        os.exit(5);
    }

    // Test 6: no defaults (baseline)
    r6 := no_defaults_used(1, 2, 3);
    if r6 != 6 {
        os.exit(6);
    }

    // Test 7: bool default — use default (true)
    r7 := bool_default(10);
    if r7 != 10 {
        os.exit(7);
    }

    // Test 8: bool default — override
    r8 := bool_default(10, false);
    if r8 != -10 {
        os.exit(8);
    }

    // Test 9: zero default — use default
    r9 := zero_default(42);
    if r9 != 42 {
        os.exit(9);
    }

    // Test 10: zero default — override
    r10 := zero_default(42, 8);
    if r10 != 50 {
        os.exit(10);
    }

    os.exit(0);
}
