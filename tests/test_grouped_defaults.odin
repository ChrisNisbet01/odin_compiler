package test

import "core:os"

// Grouped multi-name with a default: the default applies to the LAST
// name in the group only (per Odin semantics, "a, b: int := 10" defaults b).
grouped :: proc(a, b: int := 10) -> int {
    return a * 100 + b
}

// Mixed groups: grouped-with-default followed by a separate single default
mixed :: proc(x, y: int := 2, z: int := 3) -> int {
    return x * 100 + y * 10 + z
}

main :: proc() {
    // Test 1: only a given, b uses default 10
    r1 := grouped(5)
    if r1 != 510 {
        os.exit(1)
    }

    // Test 2: both given
    r2 := grouped(1, 2)
    if r2 != 102 {
        os.exit(2)
    }

    // Test 3: mixed — x given, y defaults 2, z defaults 3
    r3 := mixed(7)
    if r3 != 723 {
        os.exit(3)
    }

    // Test 4: mixed — x and y given, z defaults 3
    r4 := mixed(7, 8)
    if r4 != 783 {
        os.exit(4)
    }

    // Test 5: mixed — all given
    r5 := mixed(7, 8, 9)
    if r5 != 789 {
        os.exit(5)
    }

    os.exit(0)
}
