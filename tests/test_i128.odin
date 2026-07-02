package main

main :: proc() -> int {
    x: i128 = 100
    y: i128 = 200
    zu: u128 = 300

    // size_of
    z := size_of(i128)
    w := size_of(u128)

    // Arithmetic
    a: i128 = x + x
    b: i128 = x - 50
    c: i128 = x * 2
    d: i128 = x / 3
    e: i128 = -x

    // Comparison
    cmp1 := x == 100
    cmp2 := x < 200

    // min / max
    m1 := min(x, y)
    m2 := max(x, y)

    // u128 comparison
    ucmp1 := zu > 200

    result: int = 0
    if z == 16 { result = result + 1 }
    if w == 16 { result = result + 1 }
    if x == 100 { result = result + 1 }
    if y == 200 { result = result + 1 }
    if zu == 300 { result = result + 1 }
    if a == 200 { result = result + 1 }
    if b == 50 { result = result + 1 }
    if c == 200 { result = result + 1 }
    if d == 33 { result = result + 1 }
    if e == -100 { result = result + 1 }
    if cmp1 == 1 { result = result + 1 }
    if cmp2 == 1 { result = result + 1 }
    if m1 == 100 { result = result + 1 }
    if m2 == 200 { result = result + 1 }
    if ucmp1 == 1 { result = result + 1 }
    return result - 15
}
