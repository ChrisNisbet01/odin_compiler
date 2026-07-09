package main
import "core:os"

foreign libc {
    abs :: proc "c" (x: i32) -> i32 ---
}

main :: proc() {
    result := abs(-5)
    os.exit(cast(int) (result - 5))
}
