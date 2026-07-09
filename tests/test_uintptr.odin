package test
import "core:os"

main :: proc() {
    x: uintptr = 42
    y: uintptr = 100
    z: uintptr = x + y
    result: int = cast(int) z
    // 42 + 100 = 142, but we just test that uintptr works
    if result != 142 {
        os.exit(1)
    }
    os.exit(0)
}
