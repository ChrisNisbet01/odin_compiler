package linalg

@(require_results)
matrix2x2_determinant_f32 :: proc "contextless" (m: matrix[2, 2]f32) -> f32 {
    return m[0, 0]*m[1, 1] - m[0, 1]*m[1, 0]
}

@(require_results)
matrix2x2_determinant_f64 :: proc "contextless" (m: matrix[2, 2]f64) -> f64 {
    return m[0, 0]*m[1, 1] - m[0, 1]*m[1, 0]
}

@(require_results)
matrix3x3_determinant_f32 :: proc "contextless" (m: matrix[3, 3]f32) -> f32 {
    a := +m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1])
    b := -m[0, 1] * (m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0])
    c := +m[0, 2] * (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0])
    return a + b + c
}

@(require_results)
matrix3x3_determinant_f64 :: proc "contextless" (m: matrix[3, 3]f64) -> f64 {
    a := +m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1])
    b := -m[0, 1] * (m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0])
    c := +m[0, 2] * (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0])
    return a + b + c
}

determinant :: proc{
    matrix2x2_determinant_f32, 
    matrix2x2_determinant_f64, 
    matrix3x3_determinant_f32, 
    matrix3x3_determinant_f64
}