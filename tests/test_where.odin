package main
import "core:os"

my_proc :: proc(x: int) -> int where true {
    return x
}

main :: proc() {
    os.exit(my_proc(0))
}
