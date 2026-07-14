package main
import "core:os"

Point :: struct { x: int; y: int }

main :: proc() {
    pt: Point
    pt.x = 21
    p: [^]Point = &pt
    v: Point = p[0]
    os.exit(v.x - 21)
}
