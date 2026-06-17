package main

main :: proc() -> int {
    // transmute between same-size types
    a: int = 0x12345678
    b: int = transmute(int) a
    result: int = 0

    if b != 0x12345678 {
        result = 1
    }

    // f64 <-> i64 (same size, different interpretation)
    c: f64 = 1.0
    d: i64 = transmute(i64) c
    e: f64 = transmute(f64) d
    f: int = cast(int) e

    if f != 1 {
        result = result + 2
    }

    // i64 transmute identity
    g: i64 = -1
    h: i64 = transmute(i64) g

    if h != -1 {
        result = result + 4
    }

    return result
}
