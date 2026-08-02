package math

// Minimal `core:math` stub. Only f64 variants are provided.
// The test_matrix_basic.odin test only uses f64 matrices and determinants,
// so f32 overloads are not yet needed (add when required).

foreign libc {
    sqrt :: proc "c" (x: f64) -> f64 ---
    acos :: proc "c" (x: f64) -> f64 ---
    sin  :: proc "c" (x: f64) -> f64 ---
    cos  :: proc "c" (x: f64) -> f64 ---
    tan  :: proc "c" (x: f64) -> f64 ---
    fabs :: proc "c" (x: f64) -> f64 ---
    pow  :: proc "c" (x: f64, y: f64) -> f64 ---
}