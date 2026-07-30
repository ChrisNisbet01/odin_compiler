package linalg

@(require_results)
matrix1x1_determinant :: proc "contextless" (m: matrix[1, 1]$T) -> T {
    return m[0, 0]
}

@(require_results)
matrix2x2_determinant :: proc "contextless" (m: matrix[2, 2]$T) -> T {
    return m[0, 0]*m[1, 1] - m[0, 1]*m[1, 0]
}

@(require_results)
matrix3x3_determinant :: proc "contextless" (m: matrix[3, 3]$T) -> T {
    a := +m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1])
    b := -m[0, 1] * (m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0])
    c := +m[0, 2] * (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0])
    return a + b + c
}

determinant :: proc{
    matrix1x1_determinant,
    matrix2x2_determinant,
    matrix3x3_determinant
}