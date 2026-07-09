package main
import "core:os"

foo :: proc(args: ..any) -> int {
    v := args[0].(int)
    return v
}

main :: proc() {
    result := foo(42)
    os.exit(result - 42)
}
