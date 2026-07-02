package main

main :: proc() -> int {
    o1 := offset_of(struct { a: i32; b: f64; c: bool }, a)
    o2 := offset_of(struct { a: i32; b: f64; c: bool }, b)
    if o1 != 0 { return 1 }
    if o2 < 4 { return 2 }
    return 0
}
