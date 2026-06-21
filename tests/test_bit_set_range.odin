package main

main :: proc() -> int {
    // Test half-open range: bit_set[0..<8] = 8 bits = u8
    bs1: bit_set[0..<8]
    incl(&bs1, 3)
    if 3 not_in bs1 {
        return 1
    }
    if 7 in bs1 {
        return 2
    }

    // Test inclusive range: bit_set[0..7] = 8 bits = u8
    bs2: bit_set[0..7]
    incl(&bs2, 7)
    if 7 not_in bs2 {
        return 3
    }

    // Test union with range-based bit_set
    bs3a: bit_set[0..<16]
    bs3b: bit_set[0..<16]
    incl(&bs3a, 1)
    incl(&bs3b, 2)
    bs3c: bit_set[0..<16] = bs3a | bs3b
    if 1 not_in bs3c {
        return 4
    }
    if 2 not_in bs3c {
        return 5
    }

    return 0
}
