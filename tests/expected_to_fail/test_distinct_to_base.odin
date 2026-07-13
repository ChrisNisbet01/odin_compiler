package main
import "core:os"

MyInt :: distinct int

main :: proc() {
    x: MyInt = 42
    y: int
    // This should fail: cannot assign MyInt to int without cast
    y = x
    os.exit(y - 42)
}
