package main
import "core:os"

Point :: struct { x: int; y: int }

main :: proc() {
    pt: Point
    pt.x = 21
    p: [^]Point = &pt
    os.exit(p[0].x - 21)
}
