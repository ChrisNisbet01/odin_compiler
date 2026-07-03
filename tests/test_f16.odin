package main

main :: proc() -> int {
    // Basic f16 declaration and initialization
    x: f16 = 1.0
    y: f16 = 2.5
    z: f16 = x + y

    result: int = 0
    // 1.0 + 2.5 = 3.5
    if z > 3.0 && z < 4.0 {
        result = result + 1
    }

    // f16 arithmetic
    a: f16 = 10.0
    b: f16 = 3.0
    c: f16 = a / b

    if c > 3.0 && c < 4.0 {
        result = result + 1
    }

    // f16 comparison
    p: f16 = 1.0
    q: f16 = 1.0
    if p == q {
        result = result + 1
    }

    return result - 3
}
