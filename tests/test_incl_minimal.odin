package main

main :: proc() -> int {
    bs: bit_set[u8]
    incl(&bs, 3)
    if 3 not_in bs {
        return 1
    }
    return 0
}
