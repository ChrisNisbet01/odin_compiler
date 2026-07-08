package main

main :: proc() -> int {
    #assert[false]
    #assert[1 + 1 == 3]
    #assert[5 < 3]
    #assert[true && false]
    return 0
}
