package main
import "core:os"

main :: proc() {
    // type_of with type-name operand
    id_int := type_of(int)
    id_u8 := type_of(u8)
    id_string := type_of(string)
    id_bool := type_of(bool)

    // Same type → same id
    #assert[type_of(int) == type_of(int)]
    #assert[type_of(u8) == type_of(u8)]
    #assert[type_of(string) == type_of(string)]
    #assert[type_of(bool) == type_of(bool)]

    // Different types → different ids
    #assert[type_of(int) != type_of(u8)]
    #assert[type_of(int) != type_of(string)]
    #assert[type_of(u8) != type_of(string)]

    // type_of with value operand still works
    x: int = 42
    #assert[type_of(x) == type_of(int)]
    #assert[type_of(x) != type_of(u8)]

    y: string = "hello"
    #assert[type_of(y) == type_of(string)]
    #assert[type_of(y) != type_of(int)]

    // type_of(type_of(int)) should work (type_of returns typeid)
    #assert[type_of(type_of(int)) == type_of(typeid)]

    // Compound types
    ptr_int := type_of(^int)
    slice_u8 := type_of([]u8)
    arr_5_int := type_of([5]int)
    #assert[type_of(^int) == type_of(^int)]
    #assert[type_of(^int) != type_of(^u8)]
    #assert[type_of([]u8) == type_of([]u8)]
    #assert[type_of([]u8) != type_of([]int)]
    #assert[type_of([5]int) == type_of([5]int)]
    #assert[type_of([5]int) != type_of([10]int)]

    // type_of of typeid itself
    #assert[type_of(typeid) == type_of(typeid)]

    os.exit(0)
}
