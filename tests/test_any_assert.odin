package main
import "core:os"

main :: proc() {
    x: any = 42
    y := x.(int)
    s := int_to_string(y)
    print_string(1, s)
    print_string(1, "\n")
    os.exit(0)
}
