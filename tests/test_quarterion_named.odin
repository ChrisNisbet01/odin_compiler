package main

import "core:os"

main :: proc() {
    // Named construction: all-int literals default to f32 -> quaternion128
    qn := quaternion(w = 1, x = 0, y = 0, z = 0)

    // Named construction with typed components for each size
    q64 := quaternion(w = f16(1), x = f16(0), y = f16(0), z = f16(0))
    q128 := quaternion(w = f32(1), x = f32(0), y = f32(0), z = f32(0))
    q256 := quaternion(w = f64(1), x = f64(0), y = f64(0), z = f64(0))

    // Positional construction (regression check for existing form)
    qp := quaternion(1.0, 2.0, 3.0, 4.0)
    qp2 := quaternion(f32(1.0), f32(2.0), f32(3.0), f32(4.0))

    // All-integer positional literals default to f32 -> quaternion128
    qi := quaternion(1, 2, 3, 4)

    // Mixed integer + float literals -> f64 -> quaternion256
    qm := quaternion(1, 2.0, 3, 4)

    result: int = 0
    if size_of(quaternion64) == 8 { result += 1 }
    if size_of(quaternion128) == 16 { result += 1 }
    if size_of(quaternion256) == 32 { result += 1 }
    if size_of(qn) == 16 { result += 1 }
    if size_of(q64) == 8 { result += 1 }
    if size_of(q128) == 16 { result += 1 }
    if size_of(q256) == 32 { result += 1 }
    if size_of(qp) == 32 { result += 1 }
    if size_of(qp2) == 16 { result += 1 }
    if size_of(qi) == 16 { result += 1 }
    if size_of(qm) == 32 { result += 1 }

    os.exit(11 - result)
}
