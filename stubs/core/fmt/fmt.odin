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
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        v := args[arg_idx].(int)
                        print_hex(v)
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        print_value(args[arg_idx])
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

print_value :: proc(v: any) {
    // NOTE: type_ids depend on registration order in the compiler
    if type_of(v) == 8 {
        s := int_to_string(v.(int))
        print_string(s)
    } else if type_of(v) == 2 {
        s := int_to_string(v.(i8))
        print_string(s)
    } else if type_of(v) == 3 {
        s := int_to_string(v.(i32))
        print_string(s)
    } else if type_of(v) == 4 {
        s := int_to_string(v.(i64))
        print_string(s)
    } else if type_of(v) == 45 {
        s := v.(string)
        print_string(s)
    } else if type_of(v) == 14 {
        b := v.(u8)
        print_byte(b)
    } else if type_of(v) == 23 {
        b := v.(byte)
        print_byte(b)
    } else {
        print_string("<?>")
    }
}

print_hex :: proc(v: int) {
    hex_digits := "0123456789abcdef"
    if v >= 16 {
        print_hex(v / 16)
    }
    print_byte(hex_digits[v % 16])
}
