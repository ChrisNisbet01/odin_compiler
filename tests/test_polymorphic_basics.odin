package test

import "core:os"

identity :: proc($T: typeid, x: T) -> T {
    return x
}

double :: proc($T: typeid, x: T) -> T {
    return x + x
}

main :: proc() {
    a := identity(42)
    b := double(21)
    if a == 42 && b == 42 {
        os.exit(0)
    }
    os.exit(1)
}
