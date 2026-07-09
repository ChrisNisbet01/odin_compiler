package main
import "core:os"

main :: proc() {
    o1 := offset_of(struct { a: i32; b: f64; c: bool }, a)
    o2 := offset_of(struct { a: i32; b: f64; c: bool }, b)
    if o1 != 0 { os.exit(1) }
    if o2 < 4 { os.exit(2) }
    os.exit(0)
}
