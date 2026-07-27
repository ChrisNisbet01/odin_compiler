package test

import "core:fmt"

test_make_without_allocator :: proc() {
    arr := make([dynamic]int, 5);
    arr[0] = 0;
    arr[1] = 2;
    arr[2] = 4;
    arr[3] = 6;
    arr[4] = 8;
    fmt.println("Array without allocator: PASS");
}

main :: proc() {
    test_make_without_allocator();
}