package main

foo :: proc(args: ..any) -> int {
    // Print the raw typeid from the any struct for debugging
    // Just access v and check if it's int
    v := args[0]
    // Force a type check that will crash if wrong type
    result := v.(int)
    return result
}

main :: proc() -> int {
    result := foo(42)
    return result - 42
}
