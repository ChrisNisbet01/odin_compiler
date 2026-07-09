package main
import "core:os"

main :: proc() {
    // Test 1: Variable decl with any (already worked)
    a: any = 42
    b: any = 3.14

    // Test 2: Assignment to existing any variable
    x: any
    x = 100
    x = 200

    // Test 3: Type assertion on any (extract int)
    y: any = 42
    z := y.(int)

    // Test 4: Arithmetic on extracted value
    result := z + 10

    // Verify: z should be 42, result should be 52
    // Return 0 if result == 52, else 1
    if result != 52 {
        os.exit(1)
    }

    os.exit(0)
}
