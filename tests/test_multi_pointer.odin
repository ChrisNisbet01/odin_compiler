package main
import "core:os"

main :: proc() {
    x: int = 42
    p: [^]int = &x
    v: int = p[0]
    os.exit(v - 42)
}
