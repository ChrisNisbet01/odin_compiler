package main

import "core:os"

// Polymorphic struct with a single type parameter
Box :: struct($T: typeid) {
    val: T
}

main :: proc() {
    result := 0

    // 1. Instantiate Box with int, write/read val
    bi: Box(int)
    bi.val = 42
    if bi.val != 42 {
        result = result + 1
    }

    // 2. Instantiate Box with f64, write/read val
    bf: Box(f64)
    bf.val = 3.14
    if bf.val != 3.14 {
        result = result + 2
    }

    // 3. Multiple instantiations do not interfere
    if bi.val != 42 {
        result = result + 4
    }
    if bf.val != 3.14 {
        result = result + 8
    }

    // 4. Instantiate Box with different integer type
    bu: Box(u32)
    bu.val = 7
    if bu.val != 7 {
        result = result + 16
    }

    // 5. Two short declarations with the same poly struct type
    bi2: Box(int)
    bi2.val = 100
    if bi2.val != 100 {
        result = result + 32
    }

    os.exit(result)
}
