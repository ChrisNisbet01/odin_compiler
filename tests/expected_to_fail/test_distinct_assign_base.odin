package main
import "core:os"

MyInt :: distinct int

main :: proc() {
    x: MyInt
    y: int = 42
    // This should fail: cannot assign int to MyInt without cast
    x = y
    os.exit(cast(int) x - 42)
}
