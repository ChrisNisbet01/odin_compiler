package linalg

@(require_results)
matrix1x1_determinant :: proc "contextless" (m: $M/matrix[1, 1]$T) -> T {
    return m[0, 0]
}

@(require_results)
matrix2x2_determinant :: proc "contextless" (m: $M/matrix[2, 2]$T) -> T {
    return m[0, 0]*m[1, 1] - m[0, 1]*m[1, 0]
}

@(require_results)
matrix3x3_determinant :: proc "contextless" (m: $M/matrix[3, 3]$T) -> T {
    a := +m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1])
    b := -m[0, 1] * (m[1, 0] * m[2, 2] - m[1, 2] * m[2, 0])
    c := +m[0, 2] * (m[1, 0] * m[2, 1] - m[1, 1] * m[2, 0])
    return a + b + c
}

@(require_results)
matrix4x4_determinant :: proc "contextless" (m: $M/matrix[4, 4]$T) -> T {
	s0 := m[0, 0] * m[1, 1] - m[1, 0] * m[0, 1]
	s1 := m[0, 0] * m[1, 2] - m[1, 0] * m[0, 2]
	s2 := m[0, 0] * m[1, 3] - m[1, 0] * m[0, 3]
	s3 := m[0, 1] * m[1, 2] - m[1, 1] * m[0, 2]
	s4 := m[0, 1] * m[1, 3] - m[1, 1] * m[0, 3]
	s5 := m[0, 2] * m[1, 3] - m[1, 2] * m[0, 3]

	c5 := m[2, 2] * m[3, 3] - m[3, 2] * m[2, 3]
	c4 := m[2, 1] * m[3, 3] - m[3, 1] * m[2, 3]
	c3 := m[2, 1] * m[3, 2] - m[3, 1] * m[2, 2]
	c2 := m[2, 0] * m[3, 3] - m[3, 0] * m[2, 3]
	c1 := m[2, 0] * m[3, 2] - m[3, 0] * m[2, 2]
	c0 := m[2, 0] * m[3, 1] - m[3, 0] * m[2, 1]

    det: T = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0
	return det
}

@(require_results)
matrix5x5_determinant :: proc "contextless" (m: $M/matrix[5, 5]$T) -> (det: T) {
	return matrix_determinant_generic(m)
}

@(require_results)
matrix6x6_determinant :: proc "contextless" (m: $M/matrix[6, 6]$T) -> (det: T) {
	return matrix_determinant_generic(m)
}

@(require_results)
matrix7x7_determinant :: proc "contextless" (m: $M/matrix[7, 7]$T) -> (det: T) {
	return matrix_determinant_generic(m)
}

@(require_results)
matrix8x8_determinant :: proc "contextless" (m: $M/matrix[8, 8]$T) -> (det: T) {
	return matrix_determinant_generic(m)
}

@(require_results)
matrix_determinant_generic :: proc "contextless" (a: $M/matrix[$N, N]$T) -> T {
	when N == 1 {
		return matrix1x1_determinant(a)
	} else when N == 2 {
		return matrix2x2_determinant(a)
	} else when N == 3 {
		return matrix3x3_determinant(a)
	} else when N == 4 {
		return matrix4x4_determinant(a)
	} else {
		a := a
		det: T = 1

		for col in 0..<N {
			pivot_row := col
			pivot_val := abs(a[col, col])
			for row in (col + 1)..<N {
				val := abs(a[row, col])
				if val > pivot_val {
					pivot_val = val
					pivot_row = row
				}
			}

			if pivot_val == 0 {
				return 0
			}

			if pivot_row != col {
				for k in 0..<N {
					t := a[col, k]
					a[col, k] = a[pivot_row, k]
					a[pivot_row, k] = t
				}
				det = -det
			}

			det *= a[col, col]

			inv_pivot := 1.0 / a[col, col]
			for row in (col + 1)..<N {
				factor := a[row, col] * inv_pivot
				for k in (col + 1)..<N {
					a[row, k] -= factor * a[col, k]
				}
			}
		}

		return det
	}
}

determinant :: proc{
    matrix1x1_determinant,
    matrix2x2_determinant,
    matrix3x3_determinant,
    matrix4x4_determinant,
    matrix5x5_determinant,
    matrix6x6_determinant,
    matrix7x7_determinant,
    matrix8x8_determinant
}