package main
import "core:os"

test_proc :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 { print_string(1, " ") }
        v := args[i]
        s := int_to_string(v.(int))
        print_string(1, s)
    }
    print_string(1, "\n")
}

main :: proc() {
    test_proc(42, -7, 0, 100)
    os.exit(0)
}
