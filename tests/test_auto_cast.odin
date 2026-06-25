package test_auto_cast

get_wide :: proc() -> int
{
    val: i32 = 99
    return auto_cast val
}

main :: proc()
{
    // Integer widening: i8 -> i16 -> i32 -> i64
    a: i8 = 42
    b: i16 = auto_cast a
    if b != 42 { return }

    c: i32 = auto_cast b
    if c != 42 { return }

    d: i64 = auto_cast c
    if d != 42 { return }

    // Integer to float
    e: f64 = auto_cast d
    if e < 41.9 || e > 42.1 { return }

    // Float to integer (truncation)
    g: f64 = 3.99
    h: i64 = auto_cast g
    if h != 3 { return }

    // Integer narrowing
    i: i64 = 12345
    j: i32 = auto_cast i
    if j != 12345 { return }

    k: i16 = auto_cast j
    if k != 12345 { return }

    // auto_cast in return context
    l := get_wide()
    if l != 99 { return }
}
