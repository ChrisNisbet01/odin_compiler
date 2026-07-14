package main
import "core:os"

main :: proc() {
    x := 10

    // if-do + else-if-do + else-do
    if x == 5 {
        os.exit(1)
    } else if x == 10 do os.exit(0)
    else do os.exit(2)

    os.exit(3)
}
