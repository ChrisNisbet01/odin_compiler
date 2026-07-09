package main
import "core:os"

main :: proc() {
    x: int = 5
    y: int = 10
    if x > 10 {
        y = 100
    } else {
        y = 200
    }
    os.exit(y - y)
}
