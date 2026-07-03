package main

main :: proc() -> int {
    // Basic typeid declaration and assignment
    t: typeid = 0
    t = 42

    // typeid arithmetic (it's essentially uintptr)
    x: typeid = 10
    y: typeid = 32
    z: typeid = x + y
    result: int = cast(int) z

    if result != 42 {
        return 1
    }
    return 0
}
