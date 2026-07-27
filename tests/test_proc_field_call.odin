package main

import "core:os"

add :: proc(a: int, b: int) -> int {
    return a + b
}

Op :: struct {
    fn:  proc(a: int, b: int) -> int;
    tag: int;
}

main :: proc() {
    result: int = 0

    op := Op{fn = add, tag = 42}
    r := op.fn(3, 4)
    if r != 7 {
        result = result + 1
    }
    if op.tag != 42 {
        result = result + 2
    }

    // Call through pointer
    op2: ^Op = &op
    r2 := op2.fn(10, 20)
    if r2 != 30 {
        result = result + 4
    }

    os.exit(result)
}
