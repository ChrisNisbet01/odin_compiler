package main
import "core:os"

MyStruct :: struct { x: int; y: int }

main :: proc() {
    s: MyStruct
    s.x = 10
    s.y = 20
    os.exit(s.x + s.y - 30)
}
