package main

foreign libc {
    abs :: proc "c" (x: i32) -> i32 ---
}

main :: proc() -> int {
    result := abs(-5)
    return cast(int) (result - 5)
}
