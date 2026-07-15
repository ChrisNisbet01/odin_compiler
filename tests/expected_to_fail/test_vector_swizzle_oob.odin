package main

main :: proc() {
    v: #simd [2]f32 = {}
    _ = v.z  // z is index 2, lane_count is 2 -> out of bounds
}
