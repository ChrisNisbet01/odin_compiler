package main
import "core:os"

// Just test that the type assertion from packed variadic works
check_type :: proc(x: any) -> bool {
    v := x.(int)
    return v == 42
}

main :: proc() {
    result: bool = check_type(42)
    os.exit(result ? 0 : 1)
}
