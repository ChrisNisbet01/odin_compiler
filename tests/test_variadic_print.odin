package main

test_proc :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 { print_string(" ") }
        v := args[i]
        s := int_to_string(v.(int))
        print_string(s)
    }
    print_string("\n")
}

main :: proc() -> int {
    test_proc(42, -7, 0, 100)
    return 0
}
