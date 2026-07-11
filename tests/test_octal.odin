package test_octal

import "core:os"

main :: proc() {
    // Basic octal parsing
    a := 0o644        // decimal 420
    b := 0o777        // decimal 511
    c := 0o10         // decimal 8
    d := 0o0          // decimal 0
    e := 0o377        // decimal 255

    // Verify via arithmetic to confirm correct parsing
    if a != 420 {
        os.exit(1)
    }
    if b != 511 {
        os.exit(2)
    }
    if c != 8 {
        os.exit(3)
    }
    if d != 0 {
        os.exit(4)
    }
    if e != 255 {
        os.exit(5)
    }

    os.exit(0)
}
