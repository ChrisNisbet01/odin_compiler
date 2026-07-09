package main
import "core:os"

main :: proc() {
    bs: bit_set[u8]
    incl(&bs, 3)
    if 3 not_in bs {
        os.exit(1)
    }
    os.exit(0)
}
