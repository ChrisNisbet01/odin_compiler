package main

main :: proc() -> int {
    x: any = 42
    y := x.(int)
    s := int_to_string(y)
    print_string(s)
    print_string("\n")
    return 0
}
