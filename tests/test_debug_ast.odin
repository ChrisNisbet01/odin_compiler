package main
import "core:os"

main :: proc() {
    uv: union { v: i32 }
    uv.v = 5
    uv.v = uv.v + 3
    if uv.v != 8 {
        os.exit(1)
    }
    os.exit(0)
}
