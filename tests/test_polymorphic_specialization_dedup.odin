package test

import "core:os"

double :: proc($T: typeid, x: T) -> T {
    return x + x
}

main :: proc() {
    a := double(21)
    b := double(42)
    c := double(100)
    // All three are int specializations — should share a single cached function
    if a == 42 && b == 84 && c == 200 {
        os.exit(0)
    }
    os.exit(1)
}
