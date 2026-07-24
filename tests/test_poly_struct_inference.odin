package main

import "core:os"

Box :: struct($T: typeid) {
    val: T
}

Pair :: struct($A: typeid, $B: typeid) {
    first: A;
    second: B
}

Triple :: struct($A: typeid, $B: typeid, $C: typeid) {
    a: A;
    b: B;
    c: C
}

get_val :: proc(b: Box(int)) -> int {
    return b.val
}

main :: proc() {
    result := 0

    // 1. Single param inference: Box{val=42} -> Box(int)
    bi := Box{val=42}
    if bi.val != 42 {
        result = result + 1
    }

    // 2. Single param, f64: Box{val=3.14} -> Box(f64)
    bf := Box{val=3.14}
    if bf.val != 3.14 {
        result = result + 2
    }

    // 3. Two param inference: Pair{first=10, second=20.0} -> Pair(int, f64)
    p := Pair{first=10, second=20.0}
    if p.first != 10 {
        result = result + 4
    }
    if p.second != 20.0 {
        result = result + 8
    }

    // 4. Three param inference: Triple{a=1, b=2.0, c=3} -> Triple(int, f64, int)
    t := Triple{a=1, b=2.0, c=3}
    if t.a != 1 {
        result = result + 16
    }
    if t.b != 2.0 {
        result = result + 32
    }
    if t.c != 3 {
        result = result + 64
    }

    // 5. Field assignment on inferred struct
    bi.val = 100
    if bi.val != 100 {
        result = result + 128
    }

    // 6. Passing inferred struct to function expecting explicit poly type
    r := get_val(bi)
    if r != 100 {
        result = result + 256
    }

    // 7. Explicit type args still work alongside inference
    be := Box(int){val=999}
    if be.val != 999 {
        result = result + 512
    }

    // 8. Inference with two same-type params
    pp := Pair{first=1, second=2}
    if pp.first != 1 || pp.second != 2 {
        result = result + 1024
    }

    os.exit(result)
}
