package main
import "core:os"

main :: proc() {
    result: int = 0

    // Test basic bit_set variable declaration
    bs: bit_set[u8]

    // Test 'in' on empty bit_set
    if 0 in bs {
        result = result + 1
    }

    // Test 'not_in' on empty bit_set
    if 0 not_in bs {
        // pass
    } else {
        result = result + 2
    }

    // Test bit_set[u16]
    bs16: bit_set[u16]
    if 5 in bs16 {
        result = result + 4
    }
    if 5 not_in bs16 {
        // pass
    } else {
        result = result + 8
    }

    // Test bit_set[u32]
    bs32: bit_set[u32]
    if 100 in bs32 {
        result = result + 16
    }

    // Test bit_set[u64]
    bs64: bit_set[u64]
    if 63 in bs64 {
        result = result + 32
    }
    if 0 not_in bs64 {
        // pass
    } else {
        result = result + 64
    }

    // Test 'in' with constant 0 on empty bit_set[u8]
    if 7 in bs {
        result = result + 128
    }

    os.exit(result)
}
