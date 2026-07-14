package main
import "core:os"

Point :: struct { x: int; y: int }

main :: proc() {
    arr: [5]Point
    arr[0].x = 21
    os.exit(arr[0].x - 21)
}
