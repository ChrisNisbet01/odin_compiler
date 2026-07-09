package main
import "core:os"

main :: proc() {
    // typeid_of with basic types should match type_of
    if typeid_of(int) != type_of(int) { os.exit(1) }
    if typeid_of(i8) != type_of(i8) { os.exit(2) }
    if typeid_of(i32) != type_of(i32) { os.exit(3) }
    if typeid_of(i64) != type_of(i64) { os.exit(4) }
    if typeid_of(u8) != type_of(u8) { os.exit(5) }
    if typeid_of(string) != type_of(string) { os.exit(6) }
    if typeid_of(bool) != type_of(bool) { os.exit(7) }

    // typeid_of with compound types
    if typeid_of(^int) != type_of(^int) { os.exit(8) }
    if typeid_of([]u8) != type_of([]u8) { os.exit(9) }
    if typeid_of([5]int) != type_of([5]int) { os.exit(10) }

    // typeid_of is compile-time constant
    x: typeid = typeid_of(int)
    if x != type_of(int) { os.exit(11) }

    // typeid_of(typeid) should match type_of(typeid)
    if typeid_of(typeid) != type_of(typeid) { os.exit(12) }

    os.exit(0)
}
