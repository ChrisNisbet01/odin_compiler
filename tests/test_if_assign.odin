package main
import "core:os"

main :: proc() {
    x: int = 5
    y: int = 10
    if x < 10 {
        y = 20
    }
    os.exit(y - 20)
}
