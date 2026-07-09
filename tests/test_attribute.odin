package main

@(require_results)
add_one :: proc(x: i32) -> i32 {
    return x + 1
}

@(link_name="custom_add")
add_two :: proc(x: i32) -> i32 {
    return x + 2
}

@(link_name="custom_mul", require_results)
mul :: proc(x: i32, y: i32) -> i32 {
    return x * y
}

main :: proc() -> int {
    _ := add_one(10)
    _ := add_two(20)
    _ := mul(6, 7)
    return 0
}
