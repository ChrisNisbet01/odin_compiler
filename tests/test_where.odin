package main

my_proc :: proc(x: int) -> int where true {
    return x
}

main :: proc() -> int {
    return my_proc(0)
}
