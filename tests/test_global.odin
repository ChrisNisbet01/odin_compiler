package main
import "core:os"

x: int = 42

main :: proc() {
    os.exit(x - x)
}
