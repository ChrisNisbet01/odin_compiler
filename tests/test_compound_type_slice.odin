package main
import "core:os"

Point :: struct { x: int; y: int }

main :: proc() {
    sl := make([]Point, 3)
    sl[0].x = 21
    os.exit(sl[0].x - 21)
}
