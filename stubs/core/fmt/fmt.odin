package fmt

println :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(" ")
        }
        v := args[i]
        if type_of(v) == 8 {
            s := int_to_string(v.(int))
            print_string(s)
        } else {
            print_string(v.(string))
        }
    }
    print_string("\n")
}
