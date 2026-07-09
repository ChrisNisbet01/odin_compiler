package main
import "core:os"

main :: proc() {
    x: int = 5
    x = 10
    os.exit(x - x)
}
