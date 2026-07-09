package main
import "core:os"

main :: proc() {
    s: struct { x: int; y: int }
    s.x = 10
    s.y = 20
    os.exit(s.x + s.y - 30)
}
