package main

main :: proc() -> int {
    // typeid_of with basic types should match type_of
    if typeid_of(int) != type_of(int) { return 1 }
    if typeid_of(i8) != type_of(i8) { return 2 }
    if typeid_of(i32) != type_of(i32) { return 3 }
    if typeid_of(i64) != type_of(i64) { return 4 }
    if typeid_of(u8) != type_of(u8) { return 5 }
    if typeid_of(string) != type_of(string) { return 6 }
    if typeid_of(bool) != type_of(bool) { return 7 }

    // typeid_of with compound types
    if typeid_of(^int) != type_of(^int) { return 8 }
    if typeid_of([]u8) != type_of([]u8) { return 9 }
    if typeid_of([5]int) != type_of([5]int) { return 10 }

    // typeid_of is compile-time constant
    x: typeid = typeid_of(int)
    if x != type_of(int) { return 11 }

    // typeid_of(typeid) should match type_of(typeid)
    if typeid_of(typeid) != type_of(typeid) { return 12 }

    return 0
}
