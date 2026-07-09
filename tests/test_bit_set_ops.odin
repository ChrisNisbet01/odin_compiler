package main
import "core:os"

main :: proc() {
    result: int = 0

    // Test 1: incl basic
    bs1: bit_set[u8]
    incl(&bs1, 3)
    if 3 not_in bs1 {
        result = result + 1
    }

    // Test 2: incl multiple elements
    bs2: bit_set[u8]
    incl(&bs2, 0)
    incl(&bs2, 1)
    incl(&bs2, 7)
    if 0 not_in bs2 {
        result = result + 2
    }
    if 1 not_in bs2 {
        result = result + 4
    }
    if 7 not_in bs2 {
        result = result + 8
    }
    if 2 in bs2 {
        result = result + 16
    }

    // Test 3: excl basic
    bs3: bit_set[u8]
    incl(&bs3, 5)
    excl(&bs3, 5)
    if 5 in bs3 {
        result = result + 32
    }

    // Test 4: excl non-existent element
    bs4: bit_set[u8]
    excl(&bs4, 3)
    if 3 in bs4 {
        result = result + 64
    }

    // Test 5: bit_set | bit_set (union)
    bs5a: bit_set[u8]
    bs5b: bit_set[u8]
    incl(&bs5a, 1)
    incl(&bs5a, 2)
    incl(&bs5b, 2)
    incl(&bs5b, 3)
    bs5c: bit_set[u8] = bs5a | bs5b
    if 1 not_in bs5c {
        result = result + 128
    }
    if 2 not_in bs5c {
        result = result + 256
    }
    if 3 not_in bs5c {
        result = result + 512
    }

    // Test 6: bit_set & bit_set (intersection)
    bs6a: bit_set[u8]
    bs6b: bit_set[u8]
    incl(&bs6a, 1)
    incl(&bs6a, 2)
    incl(&bs6b, 2)
    incl(&bs6b, 3)
    bs6c: bit_set[u8] = bs6a & bs6b
    if 1 in bs6c {
        result = result + 1024
    }
    if 2 not_in bs6c {
        result = result + 2048
    }
    if 3 in bs6c {
        result = result + 4096
    }

    // Test 7: bit_set - bit_set (difference / AND-NOT)
    bs7a: bit_set[u8]
    bs7b: bit_set[u8]
    incl(&bs7a, 1)
    incl(&bs7a, 2)
    incl(&bs7a, 3)
    incl(&bs7b, 2)
    bs7c: bit_set[u8] = bs7a - bs7b
    if 1 not_in bs7c {
        result = result + 8192
    }
    if 2 in bs7c {
        result = result + 16384
    }
    if 3 not_in bs7c {
        result = result + 32768
    }

    // Test 8: bit_set ~ bit_set (symmetric difference / XOR)
    bs8a: bit_set[u8]
    bs8b: bit_set[u8]
    incl(&bs8a, 1)
    incl(&bs8a, 2)
    incl(&bs8b, 2)
    incl(&bs8b, 3)
    bs8c: bit_set[u8] = bs8a ~ bs8b
    if 1 not_in bs8c {
        result = result + 65536
    }
    if 2 in bs8c {
        result = result + 131072
    }
    if 3 not_in bs8c {
        result = result + 262144
    }

    // Test 9: compound assignment |=
    bs9a: bit_set[u8]
    bs9b: bit_set[u8]
    incl(&bs9a, 1)
    incl(&bs9b, 2)
    bs9a |= bs9b
    if 1 not_in bs9a {
        result = result + 524288
    }
    if 2 not_in bs9a {
        result = result + 1048576
    }

    // Test 10: compound assignment &=
    bs10a: bit_set[u8]
    bs10b: bit_set[u8]
    incl(&bs10a, 1)
    incl(&bs10a, 2)
    incl(&bs10b, 2)
    incl(&bs10b, 3)
    bs10a &= bs10b
    if 1 in bs10a {
        result = result + 2097152
    }
    if 2 not_in bs10a {
        result = result + 4194304
    }
    if 3 in bs10a {
        result = result + 8388608
    }

    // Test 11: compound assignment -= (with bit_set)
    bs11a: bit_set[u8]
    bs11b: bit_set[u8]
    incl(&bs11a, 1)
    incl(&bs11a, 2)
    incl(&bs11a, 3)
    incl(&bs11b, 2)
    bs11a -= bs11b
    if 1 not_in bs11a {
        result = result + 16777216
    }
    if 2 in bs11a {
        result = result + 33554432
    }
    if 3 not_in bs11a {
        result = result + 67108864
    }

    // Test 12: compound assignment ~=
    bs12a: bit_set[u8]
    bs12b: bit_set[u8]
    incl(&bs12a, 1)
    incl(&bs12a, 2)
    incl(&bs12b, 2)
    incl(&bs12b, 3)
    bs12a ~= bs12b
    if 1 not_in bs12a {
        result = result + 134217728
    }
    if 2 in bs12a {
        result = result + 268435456
    }
    if 3 not_in bs12a {
        result = result + 536870912
    }

    os.exit(result)
}
