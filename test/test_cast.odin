package main

main :: proc() -> int {
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

    // pointer -> int
    m: int = 123
    n: ^int = &m
    o: int = cast(int) n

    if o != 123 {
        result = result + 16
    }

    return result
}
