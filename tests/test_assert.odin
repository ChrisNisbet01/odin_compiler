package main

main :: proc() -> int {
    #assert[true]
    #assert[!false]
    #assert[1 + 1 == 2]
    #assert[1 + 2 * 3 == 7]
    #assert[(1 + 2) * 3 == 9]
    #assert[10 / 2 == 5]
    #assert[10 % 3 == 1]
    #assert[5 - 3 == 2]
    #assert[-5 + 5 == 0]
    #assert[5 > 3]
    #assert[3 < 5]
    #assert[5 >= 5]
    #assert[5 <= 5]
    #assert[5 != 3]
    #assert[1 << 3 == 8]
    #assert[8 >> 2 == 2]
    #assert[0xFF & 0x0F == 0x0F]
    #assert[0xF0 | 0x0F == 0xFF]
    #assert[0xFF ~ 0xF0 == 0x0F]
    #assert[true && true]
    #assert[true || false]
    #assert[1 + 2 + 3 + 4 + 5 == 15]
    #assert[1 * 2 * 3 * 4 == 24]
    #assert[typeid_of(int) == typeid_of(int)]
    return 0
}
