package main
import "core:os"

main :: proc() {
    a: [3]int
    a[0] = 10
    a[1] = 20
    a[2] = 30
    os.exit(a[1] - 20)
}
