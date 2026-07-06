package main

pack_test :: proc(args: ..any) -> int {
    return len(args)
}

main :: proc() -> int {
    result: int = pack_test(42, -7, 0, 100)
    return result - 4
}
