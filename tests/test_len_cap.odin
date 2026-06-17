package main

main :: proc() -> int {
    result: int = 0

    // len on fixed array (compile-time constant)
    arr: [5]int
    if len(arr) != 5 {
        result = result + 1
    }

    // cap on fixed array == len
    if cap(arr) != 5 {
        result = result + 2
    }

    // len on string variable
    s: string = "hello"
    if len(s) != 5 {
        result = result + 4
    }

    // len on string literal
    if len("world") != 5 {
        result = result + 8
    }

    return result
}
