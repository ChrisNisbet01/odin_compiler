package main

main :: proc() {
    v: #simd [4]f32 = {}
    _ = v.xg  // mixed sets: x (pos) and g (color)
}
