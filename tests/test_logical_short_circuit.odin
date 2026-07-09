package main
import "core:os"

main :: proc() {
    result := 0

    // Test 1: && short-circuit when LHS is false (RHS can't affect result)
    if (1 == 2) && (2 == 2) {
        result = 1
    }

    // Test 2: && both true
    if !((1 == 1) && (2 == 2)) {
        result = result + 2
    }

    // Test 3: && LHS false, RHS true overall false
    if (1 == 2) && (2 == 2) {
        result = result + 4
    }

    // Test 4: && LHS true, RHS false overall false
    if (1 == 1) && (2 == 3) {
        result = result + 8
    }

    // Test 5: || LHS true short-circuits
    if !((1 == 1) || (2 == 2)) {
        result = result + 16
    }

    // Test 6: || both false
    if (1 == 2) || (2 == 3) {
        result = result + 32
    }

    // Test 7: || LHS false, RHS true overall true
    if !((1 == 2) || (2 == 2)) {
        result = result + 64
    }

    // Test 8: used in if condition
    x := 0
    if (1 == 1) && (2 == 2) {
        x = 100
    }
    if x != 100 {
        result = result + 128
    }

    // Test 9: || in if condition
    y := 0
    if (1 == 2) || (2 == 2) {
        y = 200
    }
    if y != 200 {
        result = result + 256
    }

    // Test 10: Chained && 
    if !((1 == 1) && (2 == 2) && (3 == 3)) {
        result = result + 512
    }

    // Test 11: Chained ||
    if !((1 == 2) || (2 == 3) || (3 == 3)) {
        result = result + 1024
    }

    // Test 12: Mixed && and ||
    if !((1 == 1) && (2 == 2) || (3 == 4)) {
        result = result + 2048
    }

    os.exit(result)
}
