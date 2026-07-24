package main

import "core:os"

Box :: struct($T: typeid) {
    val: T
}

PtrBox :: struct($T: typeid) {
    val: T;
    ptr: ^T
}

ArrayBox :: struct($T: typeid, $N: int) {
    data: [$N]T
}

SliceBox :: struct($T: typeid) {
    data: []T
}

// Function that takes a poly struct by value
get_val :: proc(b: Box(int)) -> int {
    return b.val
}

// Function that returns a poly struct
make_box :: proc(v: int) -> Box(int) {
    b: Box(int)
    b.val = v
    return b
}

// Function that takes a poly struct pointer
inc_val :: proc(b: ^Box(int)) {
    b.val = b.val + 1
}

main :: proc() {
    result := 0

    // 1. Poly struct with ^T field
    pb: PtrBox(int)
    pb.val = 10
    x := 20
    pb.ptr = &x
    if pb.val != 10 {
        result = result + 1
    }
    if pb.ptr^ != 20 {
        result = result + 2
    }

    // 2. len() on poly struct array field
    ab: ArrayBox(int, 4)
    ab.data[0] = 1
    ab.data[1] = 2
    ab.data[2] = 3
    ab.data[3] = 4
    n := len(ab.data)
    if n != 4 {
        result = result + 4
    }

    // 3. Passing poly struct as function argument
    b1: Box(int)
    b1.val = 77
    r1 := get_val(b1)
    if r1 != 77 {
        result = result + 8
    }

    // 4. Returning poly struct from function
    b2 := make_box(99)
    if b2.val != 99 {
        result = result + 16
    }

    // 5. Pointer to poly struct
    b3: Box(int)
    b3.val = 50
    inc_val(&b3)
    if b3.val != 51 {
        result = result + 32
    }

    // 6. Poly struct with f64 + ^T field
    pbf: PtrBox(f64)
    pbf.val = 1.5
    y := 2.5
    pbf.ptr = &y
    if pbf.val != 1.5 {
        result = result + 64
    }
    if pbf.ptr^ != 2.5 {
        result = result + 128
    }

    // 7. Poly struct with []T (slice) field
    sbox: SliceBox(int)
    sbox.data = make([]int, 3)
    sbox.data[0] = 100
    sbox.data[1] = 200
    sbox.data[2] = 300
    if sbox.data[0] != 100 {
        result = result + 256
    }
    if sbox.data[2] != 300 {
        result = result + 512
    }
    if len(sbox.data) != 3 {
        result = result + 1024
    }

    // 8. Accumulate through poly struct array field
    s := 0
    for i in 0..<4 {
        s += ab.data[i]
    }
    if s != 10 {
        result = result + 2048
    }

    os.exit(result)
}
