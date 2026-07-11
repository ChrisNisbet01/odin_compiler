package main
import "core:os"

main :: proc() {
    // int -> int widening
    a: int = 42
    b: i64 = cast(i64) a
    result: int = 0

    if b != 42 {
        result = 1
    }

    // int -> int narrowing (downcast)
    c: i64 = 1000
    d: int = cast(int) c

    if d != 1000 {
        result = result + 2
    }

    // float -> int
    e: f64 = 3.0
    f: int = cast(int) e

    if f != 3 {
        result = result + 4
    }

    // int -> float (test by casting back)
    g: int = 7
    h: f64 = cast(f64) g
    i: int = cast(int) h

    if i != 7 {
        result = result + 8
    }

    // pointer -> uintptr roundtrip (verify pointer survives int conversion)
    m: int = 123
    n: ^int = &m
    addr: uintptr = cast(uintptr) n
    p: ^int = cast(^int) addr
    if p != n {
        result = result + 16
    }

    os.exit(result)
}
