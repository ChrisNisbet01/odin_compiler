package main
import "core:os"

// Named distinct type
MyInt :: distinct int

// Named distinct type based on another integer type
MyU32 :: distinct u32

// Named distinct type based on a pointer
MyPtr :: distinct ^int

main :: proc() {
    result := 0

    // 1. Distinct type can be declared and assigned an untyped literal
    x: MyInt
    x = 42
    if x != 42 {
        result = result + 1
    }

    // 2. Same distinct type can be assigned between variables
    y: MyInt
    y = x
    if y != 42 {
        result = result + 2
    }

    // 3. Distinct type preserves the value through cast
    val: int = cast(int) x
    if val != 42 {
        result = result + 4
    }

    // 4. Distinct u32 type works with unsigned types
    u: MyU32
    u = 100
    if cast(u32) u != 100 {
        result = result + 8
    }

    // 5. Distinct type with untyped literal initialization
    z: MyInt = 99
    if z != 99 {
        result = result + 16
    }

    // 6. Cast from base type to distinct type
    base_val: int = 77
    w: MyInt = cast(MyInt) base_val
    if w != 77 {
        result = result + 32
    }

    os.exit(result)
}
