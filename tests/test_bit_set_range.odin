package main
import "core:os"

main :: proc() {
    // Test half-open range: bit_set[0..<8] = 8 bits = u8
    bs1: bit_set[0..<8]
    incl(&bs1, 3)
    if 3 not_in bs1 {
        os.exit(1)
    }
    if 7 in bs1 {
        os.exit(2)
    }

    // Test inclusive range: bit_set[0..7] = 8 bits = u8
    bs2: bit_set[0..7]
    incl(&bs2, 7)
    if 7 not_in bs2 {
        os.exit(3)
    }

    // Test union with range-based bit_set
    bs3a: bit_set[0..<16]
    bs3b: bit_set[0..<16]
    incl(&bs3a, 1)
    incl(&bs3b, 2)
    bs3c: bit_set[0..<16] = bs3a | bs3b
    if 1 not_in bs3c {
        os.exit(4)
    }
    if 2 not_in bs3c {
        os.exit(5)
    }

    os.exit(0)
}
