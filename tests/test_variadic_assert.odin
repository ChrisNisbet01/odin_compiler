package main

foo :: proc(args: ..any) -> int {
    v := args[0].(int)
    return v
}

main :: proc() -> int {
    result := foo(42)
    return result - 42
}
