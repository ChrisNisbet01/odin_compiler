package main

main :: proc() -> int {
    #assert[true]
    #assert[1 == 1]
    #assert[!(1 == 2)]
    #assert[true && true]
    #assert[true || false]
    #assert[!false]
    return 0
}
