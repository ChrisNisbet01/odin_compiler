package main

main :: proc() -> int {
    // Endian-specific integer types (on x86_64 LE, same as native)
    a: i32le = 42
    b: i32be = 100
    c: i64le = a + b

    result: int = 0
    if c == 142 {
        result = result + 1
    }

    // Unsigned variants
    x: u16le = 10
    y: u16be = 20
    z: u32le = x + y
    if z == 30 {
        result = result + 1
    }

    // Float variants
    p: f32le = 1.5
    q: f32be = 2.5
    r: f64le = p + q
    if r > 3.9 && r < 4.1 {
        result = result + 1
    }

    return result - 3
}
