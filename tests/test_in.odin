package main

main :: proc() -> int {
    result: int = 0

    arr: [5]int
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    arr[3] = 40
    arr[4] = 50

    // Test 1: Element found in array — in returns non-zero
    in_result: int = 30 in arr
    if in_result == 0 {
        result = result + 1
    }

    // Test 2: Element not found — in returns 0
    in_result2: int = 99 in arr
    if in_result2 != 0 {
        result = result + 2
    }

    // Test 3: not_in returns 0 when element IS found
    not_in_result: int = 20 not_in arr
    if not_in_result != 0 {
        result = result + 4
    }

    // Test 4: not_in returns non-zero when element NOT found
    not_in_result2: int = 99 not_in arr
    if not_in_result2 == 0 {
        result = result + 8
    }

    // Test 5: in in if condition — found
    if 10 in arr {
        // pass
    } else {
        result = result + 16
    }

    // Test 6: in in if condition — not found
    if 100 in arr {
        result = result + 32
    } else {
        // pass
    }

    // Test 7: not_in in if condition — not found (passes)
    if 99 not_in arr {
        // pass
    } else {
        result = result + 64
    }

    // Test 8: not_in in if condition — found (fails)
    if 20 not_in arr {
        result = result + 128
    } else {
        // pass
    }

    // Test 9: in with slices — found
    slc: []int = arr[..]
    if 30 in slc {
        // pass
    } else {
        result = result + 256
    }

    // Test 10: not_in with slices — not found
    if 99 not_in slc {
        // pass
    } else {
        result = result + 512
    }

    // Test 11: in with slices — not found
    if 100 in slc {
        result = result + 1024
    } else {
        // pass
    }

    return result
}
