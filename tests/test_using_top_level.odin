package main
import "core:os"

using x: int = 5

main :: proc() {
    os.exit(x - 5)
}
