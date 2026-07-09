package main
import "core:os"

main :: proc() {
    x := 5
    os.exit(x - x)
}
