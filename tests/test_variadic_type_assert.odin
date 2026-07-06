package main

// Just test that the type assertion from packed variadic works
check_type :: proc(x: any) -> bool {
    v := x.(int)
    return v == 42
}

main :: proc() -> int {
    result: bool = check_type(42)
    return result ? 0 : 1
}
