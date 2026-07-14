package main
import "core:os"

main :: proc() {
    x := 5

    // Basic if-do (true branch)
    if x == 5 do os.exit(0)
    os.exit(1)
}
