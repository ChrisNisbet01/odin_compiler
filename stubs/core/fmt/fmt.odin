package fmt

println :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(" ")
        }
        print_value(args[i])
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
                        print_value(args[arg_idx])
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
                        print_value(args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(args[arg_idx])
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
    if type_of(v) == type_of(int) {
        s := int_to_string(v.(int))
        print_string(s)
    } else if type_of(v) == type_of(i8) {
        s := int_to_string(v.(i8))
        print_string(s)
    } else if type_of(v) == type_of(i32) {
        s := int_to_string(v.(i32))
        print_string(s)
    } else if type_of(v) == type_of(i64) {
        s := int_to_string(v.(i64))
        print_string(s)
    } else if type_of(v) == type_of(string) {
        s := v.(string)
        print_string(s)
    } else if type_of(v) == type_of(u8) {
        s := int_to_string(v.(u8))
        print_string(s)
    } else if type_of(v) == type_of(u16) {
        s := int_to_string(v.(u16))
        print_string(s)
    } else if type_of(v) == type_of(u32) {
        s := int_to_string(v.(u32))
        print_string(s)
    } else if type_of(v) == type_of(u64) {
        s := int_to_string(v.(u64))
        print_string(s)
    } else if type_of(v) == type_of(byte) {
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
