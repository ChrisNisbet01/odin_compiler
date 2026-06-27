package main

main :: proc() -> int {
    uv: union { v: i32 }
    uv.v = 5
    uv.v = uv.v + 3
    if uv.v != 8 {
        return 1
    }
    return 0
}
