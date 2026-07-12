package main
import "core:os"

main :: proc() {
    #assert[false]
    #assert[1 + 1 == 3]
    #assert[5 < 3]
    #assert[true && false]
    os.exit(0)
}
