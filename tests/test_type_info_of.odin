package main
import "core:os"

main :: proc() {
    result: int = 0

    // Basic types
    ti_int := type_info_of(int)
    ti_i8  := type_info_of(i8)
    ti_i16 := type_info_of(i16)
    ti_i32 := type_info_of(i32)
    ti_i64 := type_info_of(i64)
    ti_u8  := type_info_of(u8)
    ti_bool := type_info_of(bool)
    ti_string := type_info_of(string)

    // size field (from size_of which we trust)
    if size_of(int) == 8  { result += 1 }
    if size_of(i8)  == 1  { result += 1 }
    if size_of(i16) == 2  { result += 1 }
    if size_of(i32) == 4  { result += 1 }
    if size_of(i64) == 8  { result += 1 }
    if size_of(u8)  == 1  { result += 1 }
    if size_of(bool) == 1 { result += 1 }

    // typeid comparison: type_info_of(T).id should equal typeid_of(T)
    if ti_int.id  == typeid_of(int)  { result += 1 }
    if ti_i8.id   == typeid_of(i8)   { result += 1 }
    if ti_i16.id  == typeid_of(i16)  { result += 1 }
    if ti_i32.id  == typeid_of(i32)  { result += 1 }
    if ti_i64.id  == typeid_of(i64)  { result += 1 }
    if ti_u8.id   == typeid_of(u8)   { result += 1 }
    if ti_bool.id == typeid_of(bool) { result += 1 }

    // Pointer type
    ti_ptr := type_info_of(^int)
    _ = ti_ptr

    // Two calls for same type should return same pointer
    ti_int2 := type_info_of(int)
    if ti_int == ti_int2 { result += 1 }

    // Different types should have different type_info addresses
    if ti_int != ti_i8 { result += 1 }

    // kind field (should be nonzero for all valid types)
    if ti_int.kind  != 0 { result += 1 }
    if ti_i8.kind   != 0 { result += 1 }
    if ti_string.kind != 0 { result += 1 }

    os.exit(result - 19)
}
