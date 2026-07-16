package main
import "core:os"

main :: proc() {
    result: int = 0

    a := make([]int, 5)
    a[0] = 10
    a[1] = 20
    a[2] = 30
    a[3] = 40
    a[4] = 50

    b := make([]byte, 3)
    b[0] = 1
    b[1] = 2
    b[2] = 3

    // soa_zip: should truncate to min length (3)
    z := soa_zip(a, b)

    if len(z._0) != 3 { result = result + 1 }
    if len(z._1) != 3 { result = result + 1 }

    if z._0[0] != 10 { result = result + 1 }
    if z._0[2] != 30 { result = result + 1 }
    if int(z._1[0]) != 1 { result = result + 1 }
    if int(z._1[2]) != 3 { result = result + 1 }

    // soa_unzip: extract back to tuple, destructure
    x, y := soa_unzip(z)
    if len(x) != 3 { result = result + 1 }
    if len(y) != 3 { result = result + 1 }
    if x[0] != 10 { result = result + 1 }
    if x[1] != 20 { result = result + 1 }
    if x[2] != 30 { result = result + 1 }
    if int(y[0]) != 1 { result = result + 1 }
    if int(y[2]) != 3 { result = result + 1 }

    os.exit(result)
}
