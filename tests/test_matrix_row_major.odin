package main

import "core:os"
import "core:math/linalg"

main :: proc() {
    result := 0

    // 1. #row_major directive: read/write consistency
    a: #row_major matrix[2,3]int
    a[0,0] = 1; a[0,1] = 2; a[0,2] = 3
    a[1,0] = 4; a[1,1] = 5; a[1,2] = 6
    if a[0,0] != 1 { result = result + 1 }
    if a[0,1] != 2 { result = result + 2 }
    if a[0,2] != 3 { result = result + 4 }
    if a[1,0] != 4 { result = result + 8 }
    if a[1,1] != 5 { result = result + 16 }
    if a[1,2] != 6 { result = result + 32 }

    // 2. Default (column-major) memory layout via matrix_flatten
    // column-major offsets: (0,0)=0,(1,0)=1,(0,1)=2,(1,1)=3,(0,2)=4,(1,2)=5
    cm: matrix[2,3]int
    cm[0,0] = 1; cm[0,1] = 2; cm[0,2] = 3
    cm[1,0] = 4; cm[1,1] = 5; cm[1,2] = 6
    cmf := linalg.matrix_flatten(cm)
    if cmf[0] != 1 { result = result + 64 }
    if cmf[1] != 4 { result = result + 128 }
    if cmf[2] != 2 { result = result + 256 }
    if cmf[3] != 5 { result = result + 512 }
    if cmf[4] != 3 { result = result + 1024 }
    if cmf[5] != 6 { result = result + 2048 }

    // 3. #row_major memory layout via matrix_flatten
    // row-major offsets: (0,0)=0,(0,1)=1,(0,2)=2,(1,0)=3,(1,1)=4,(1,2)=5
    rmf := linalg.matrix_flatten(a)
    if rmf[0] != 1 { result = result + 4096 }
    if rmf[1] != 2 { result = result + 8192 }
    if rmf[2] != 3 { result = result + 16384 }
    if rmf[3] != 4 { result = result + 32768 }
    if rmf[4] != 5 { result = result + 65536 }
    if rmf[5] != 6 { result = result + 131072 }

    // 4. #column_major explicit directive behaves like default
    b: #column_major matrix[2,2]int
    b[0,0] = 7; b[0,1] = 8
    b[1,0] = 9; b[1,1] = 10
    if b[0,0] != 7 { result = result + 262144 }
    if b[1,1] != 10 { result = result + 524288 }
    bf := linalg.matrix_flatten(b)
    // column-major: (0,0)=0,(1,0)=1,(0,1)=2,(1,1)=3
    if bf[0] != 7 { result = result + 1048576 }
    if bf[1] != 9 { result = result + 2097152 }
    if bf[2] != 8 { result = result + 4194304 }
    if bf[3] != 10 { result = result + 8388608 }

    // 5. transpose preserves #row_major layout
    t := linalg.transpose(a) // result is #row_major matrix[3,2]int
    if t[0,0] != 1 { result = result + 16777216 }
    if t[0,1] != 4 { result = result + 33554432 }
    if t[1,0] != 2 { result = result + 67108864 }
    if t[1,1] != 5 { result = result + 134217728 }
    if t[2,0] != 3 { result = result + 268435456 }
    if t[2,1] != 6 { result = result + 536870912 }

    // 6. Matrix multiply with #row_major LHS
    r1: #row_major matrix[2,2]int
    r1[0,0] = 1; r1[0,1] = 2
    r1[1,0] = 3; r1[1,1] = 4
    r2: #row_major matrix[2,2]int
    r2[0,0] = 5; r2[0,1] = 6
    r2[1,0] = 7; r2[1,1] = 8
    rp := r1 * r2
    if rp[0,0] != 19 { result = result + 1 }
    if rp[0,1] != 22 { result = result + 2 }
    if rp[1,0] != 43 { result = result + 4 }
    if rp[1,1] != 50 { result = result + 8 }

    // 7. Row-major matrix literal
    lit: #row_major matrix[2,2]int = #row_major matrix[2,2]int{1, 2, 3, 4}
    if lit[0,0] != 1 { result = result + 16 }
    if lit[0,1] != 2 { result = result + 32 }
    if lit[1,0] != 3 { result = result + 64 }
    if lit[1,1] != 4 { result = result + 128 }

    os.exit(result)
}
