package main

import "core:os"

// Two-param poly struct: type + int
IntBox :: struct($T: typeid, $N: int) {
    data: [$N]T
}

// Three-param poly struct: two types + int
TripleBox :: struct($A: typeid, $B: typeid, $N: int) {
    firsts:  [$N]A;
    seconds: [$N]B
}

// u32-typed integer param
Buf :: struct($N: u32, $T: typeid) {
    data: [$N]T
}

main :: proc() {
    result := 0

    // 1. IntBox(int, 3) — array subscript for byte access
    ib: IntBox(int, 3)
    ib.data[0] = 10
    ib.data[1] = 20
    ib.data[2] = 30
    s := 0
    for i in 0..<3 {
        s += ib.data[i]
    }
    if s != 60 {
        result = result + 1
    }

    // 2. IntBox(f64, 5) — float polyline accumulation
    fb: IntBox(f64, 5)
    fb.data[0] = 1.0
    fb.data[1] = 2.0
    fb.data[2] = 3.0
    fb.data[3] = 4.0
    fb.data[4] = 5.0
    sf := 0.0
    for i in 0..<5 {
        sf += fb.data[i]
    }
    if sf != 15.0 {
        result = result + 2
    }

    // 3. IntBox with same element type but different N confirms
    //    separate instantiations
    ib2: IntBox(int, 2)
    ib2.data[0] = 100
    ib2.data[1] = 200
    if ib2.data[0] != 100 || ib2.data[1] != 200 {
        result = result + 4
    }

    // 4.buf - 4-int param with u32 N
    buf: Buf(4, int)
    buf.data[0] = 1
    buf.data[1] = 2
    buf.data[2] = 3
    buf.data[3] = 4
    ss := 0
    for i in 0..<4 {
        ss += buf.data[i]
    }
    if ss != 10 {
        result = result + 8
    }

    os.exit(result)
}
