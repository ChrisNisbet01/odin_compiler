package main
import "core:os"

main :: proc() {
    // Basic 'any' variable declaration
    a: any = 42
    b: any = "hello"
    c: any = 3.14

    // Test that basic operations still work
    x: int = 5
    y: int = 10
    if x < y {
        y = 20
    }
    os.exit(y - 20)
}
