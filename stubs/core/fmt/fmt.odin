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

printf :: proc(format: string, args: ..any) {
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        v := args[arg_idx].(int)
                        s := int_to_string(v)
                        print_string(s)
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        print_string(s)
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    print_byte('%')
                }
            }
        } else {
            print_byte(format[i])
        }
        i += 1
    }
}
