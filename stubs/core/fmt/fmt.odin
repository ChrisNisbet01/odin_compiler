package fmt

println :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(1, " ")
        }
        print_value(1, args[i])
    }
    print_string(1, "\n")
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
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        print_string(1, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    print_byte(1, '%')
                }
            }
        } else {
            print_byte(1, format[i])
        }
        i += 1
    }
}

printfln :: proc(format: string, args: ..any) {
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        print_string(1, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    print_byte(1, '%')
                }
            }
        } else {
            print_byte(1, format[i])
        }
        i += 1
    }
    print_string(1, "\n")
}

// eprint variants — write to stderr (fd=2)
eprintln :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(2, " ")
        }
        print_value(2, args[i])
    }
    print_string(2, "\n")
}

eprintf :: proc(format: string, args: ..any) {
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        print_string(2, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    print_byte(2, '%')
                }
            }
        } else {
            print_byte(2, format[i])
        }
        i += 1
    }
}

eprintfln :: proc(format: string, args: ..any) {
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        print_string(2, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    print_byte(2, '%')
                }
            }
        } else {
            print_byte(2, format[i])
        }
        i += 1
    }
    print_string(2, "\n")
}

// print_value — dispatch by runtime type
print_value :: proc(fd: int, v: any) {
    if type_of(v) == type_of(int) {
        s := int_to_string(v.(int))
        print_string(fd, s)
    } else if type_of(v) == type_of(i8) {
        s := int_to_string(v.(i8))
        print_string(fd, s)
    } else if type_of(v) == type_of(i16) {
        s := int_to_string(v.(i16))
        print_string(fd, s)
    } else if type_of(v) == type_of(i32) {
        s := int_to_string(v.(i32))
        print_string(fd, s)
    } else if type_of(v) == type_of(i64) {
        s := int_to_string(v.(i64))
        print_string(fd, s)
    } else if type_of(v) == type_of(string) {
        s := v.(string)
        print_string(fd, s)
    } else if type_of(v) == type_of(u8) {
        s := int_to_string(v.(u8))
        print_string(fd, s)
    } else if type_of(v) == type_of(u16) {
        s := int_to_string(v.(u16))
        print_string(fd, s)
    } else if type_of(v) == type_of(u32) {
        s := int_to_string(v.(u32))
        print_string(fd, s)
    } else if type_of(v) == type_of(u64) {
        s := int_to_string(v.(u64))
        print_string(fd, s)
    } else if type_of(v) == type_of(uintptr) {
        s := int_to_string(v.(uintptr))
        print_string(fd, s)
    } else if type_of(v) == type_of(rune) {
        s := int_to_string(v.(rune))
        print_string(fd, s)
    } else if type_of(v) == type_of(byte) {
        b := v.(byte)
        print_byte(fd, b)
    } else if type_of(v) == type_of(f64) {
        print_f64(fd, v.(f64))
    } else if type_of(v) == type_of(f32) {
        print_f64(fd, f64(v.(f32)))
    } else if type_of(v) == type_of(bool) {
        if v.(bool) {
            print_string(fd, "true")
        } else {
            print_string(fd, "false")
        }
    } else {
        print_string(fd, "<?>")
    }
}

print_f64 :: proc(fd: int, v: f64) {
    // Handle negative
    if v < 0 {
        print_byte(fd, '-')
        v = -v
    }
    // Integer part
    int_part := int(v)
    s := int_to_string(int_part)
    print_string(fd, s)
    print_byte(fd, '.')
    // Fractional part: extract 6 digits
    frac_part := v - f64(int_part)
    for i in 0..<6 {
        frac_part *= 10.0
        digit := int(frac_part)
        print_byte(fd, u8('0' + digit))
        frac_part -= f64(digit)
    }
}

print_hex :: proc(fd: int, v: int) {
    hex_digits := "0123456789abcdef"
    if v >= 16 {
        print_hex(fd, v / 16)
    }
    print_byte(fd, hex_digits[v % 16])
}
