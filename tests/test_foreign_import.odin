package main

foreign import libm "m"

foreign libm {
    sqrt :: proc "c" (x: f64) -> f64 ---
}

main :: proc() -> int {
    result := sqrt(64.0)
    // sqrt(64.0) should be exactly 8.0
    if result < 8.0 {
        return 1
    }
    if result > 8.0 {
        return 2
    }
    return 0
}
