package main
import "core:os"

main :: proc() {
    s1 := size_of(int)
    s2 := size_of(i32)
    s3 := size_of(f64)
    s4 := size_of(bool)
    a1 := align_of(int)
    a2 := align_of(i32)
    o1 := offset_of(struct { a: i32; b: f64; c: bool }, a)
    o2 := offset_of(struct { a: i32; b: f64; c: bool }, b)
    o3 := offset_of(struct { a: i32; b: f64; c: bool }, c)
    m1 := min(10, 20)
    m2 := max(10, 20)
    m3 := min(-5, 3)
    m4 := max(-5, 3)
    result: int = 0
    if s1 == 8 { result = result + 1 }
    if s2 == 4 { result = result + 1 }
    if s3 == 8 { result = result + 1 }
    if s4 == 1 { result = result + 1 }
    if a1 == 8 { result = result + 1 }
    if a2 == 4 { result = result + 1 }
    if o1 == 0 { result = result + 1 }
    if o2 > 0 { result = result + 1 }
    if o3 > 0 { result = result + 1 }
    if m1 == 10 { result = result + 1 }
    if m2 == 20 { result = result + 1 }
    if m3 == -5 { result = result + 1 }
    if m4 == 3 { result = result + 1 }
    os.exit(result - 13)
}
