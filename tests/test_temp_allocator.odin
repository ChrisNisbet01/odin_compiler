package test

import "core:os"

main :: proc() {
    #assert[42 == 42]
    os.exit(0)
}