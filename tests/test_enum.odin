package main
import "core:os"

main :: proc() {
    c: enum { A, B, C }
    val := cast(int) B
    os.exit(val - 1)
}
