package main
import "core:os"

// Basic type aliases
Handle :: int
SignedInt :: i64
ByteType :: u8
PtrToInt :: ^int
PtrToHandle :: ^Handle

main :: proc() {
    x: Handle
    x = 42
    result := 0
    if x != 42 {
        result = 1
    }

    // Type alias preserves signedness and size
    y: SignedInt
    y = -1
    if y != -1 {
        result = result + 2
    }

    // Type alias for unsigned type
    b: ByteType
    b = 255
    if b != 255 {
        result = result + 4
    }

    // Pointer type alias: dereference
    v: int = 100
    p: PtrToInt = &v
    if p^ != 100 {
        result = result + 8
    }

    // Pointer-to-alias: dereference
    h: Handle = 77
    ph: PtrToHandle = &h
    if ph^ != 77 {
        result = result + 16
    }

    // Type alias to type alias
    new_handle :: Handle
    _ = new_handle

    os.exit(result)
}
