package main
import "core:os"

main :: proc() {
    v: struct { using inner: struct { x: int; y: int }; z: int }
    v.x = 10
    v.y = 20
    v.z = 30
    os.exit(v.x + v.y + v.z - 60)
}
