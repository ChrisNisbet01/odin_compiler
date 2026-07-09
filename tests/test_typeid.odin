package main
import "core:os"

main :: proc() {
    // Basic typeid declaration and assignment
    t: typeid = 0
    t = 42

    // typeid arithmetic (it's essentially uintptr)
    x: typeid = 10
    y: typeid = 32
    z: typeid = x + y
    result: int = cast(int) z

    if result != 42 {
        os.exit(1)
    }
    os.exit(0)
}
