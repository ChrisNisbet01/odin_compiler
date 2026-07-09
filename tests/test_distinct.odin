package main
import "core:os"

main :: proc() {
    x: distinct int
    x = 42
    os.exit(cast(int) x - 42)
}
