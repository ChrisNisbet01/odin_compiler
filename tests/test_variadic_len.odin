package main
import "core:os"

pack_test :: proc(args: ..any) -> int {
    return len(args)
}

main :: proc() {
    result: int = pack_test(42, -7, 0, 100)
    os.exit(result - 4)
}
