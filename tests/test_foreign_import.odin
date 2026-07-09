package main
import "core:os"

foreign import libm "m"

foreign libm {
    sqrt :: proc "c" (x: f64) -> f64 ---
}

main :: proc() {
    result := sqrt(64.0)
    // sqrt(64.0) should be exactly 8.0
    if result < 8.0 {
        os.exit(1)
    }
    if result > 8.0 {
        os.exit(2)
    }
    os.exit(0)
}
