package fmt

import "core:strings"

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
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_hex_upper(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_binary(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_octal(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
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
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_hex_upper(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(1, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_binary(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_octal(1, v.(int))
                        } else {
                            print_value(1, args[arg_idx])
                        }
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
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_hex_upper(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_binary(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_octal(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
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
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_hex_upper(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        print_value(2, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_binary(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            print_octal(2, v.(int))
                        } else {
                            print_value(2, args[arg_idx])
                        }
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
    if v < 0 {
        print_byte(fd, '-')
        v = -v
    }
    int_part := int(v)
    s := int_to_string(int_part)
    print_string(fd, s)
    print_byte(fd, '.')
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
    is_neg := v < 0
    if is_neg {
        print_byte(fd, '-')
        v = -v
    }
    if v >= 16 {
        print_hex(fd, v / 16)
    }
    print_byte(fd, hex_digits[v % 16])
}

print_hex_upper :: proc(fd: int, v: int) {
    hex_digits := "0123456789ABCDEF"
    is_neg := v < 0
    if is_neg {
        print_byte(fd, '-')
        v = -v
    }
    if v >= 16 {
        print_hex_upper(fd, v / 16)
    }
    print_byte(fd, hex_digits[v % 16])
}

print_binary :: proc(fd: int, v: int) {
    if v == 0 {
        print_byte(fd, '0')
        return
    }
    if v < 0 {
        print_byte(fd, '-')
        v = -v
    }
    bits: [64]u8
    i := 0
    for v > 0 {
        bits[i] = u8('0' + (v & 1))
        v = v >> 1
        i += 1
    }
    for i > 0 {
        i -= 1
        print_byte(fd, bits[i])
    }
}

print_octal :: proc(fd: int, v: int) {
    if v == 0 {
        print_byte(fd, '0')
        return
    }
    if v < 0 {
        print_byte(fd, '-')
        v = -v
    }
    digits: [22]u8
    i := 0
    for v > 0 {
        digits[i] = u8('0' + (v & 7))
        v = v >> 3
        i += 1
    }
    for i > 0 {
        i -= 1
        print_byte(fd, digits[i])
    }
}

// --- strings.Builder output helpers ---

sb_print_string :: proc(b: ^strings.Builder, s: string) {
    strings.write_string(b, s)
}

sb_print_byte :: proc(b: ^strings.Builder, c: byte) {
    strings.write_byte(b, c)
}

sb_print_value :: proc(b: ^strings.Builder, v: any) {
    if type_of(v) == type_of(int) {
        sb_print_int(b, v.(int))
    } else if type_of(v) == type_of(i8) {
        sb_print_int(b, int(v.(i8)))
    } else if type_of(v) == type_of(i16) {
        sb_print_int(b, int(v.(i16)))
    } else if type_of(v) == type_of(i32) {
        sb_print_int(b, int(v.(i32)))
    } else if type_of(v) == type_of(i64) {
        sb_print_int(b, int(v.(i64)))
    } else if type_of(v) == type_of(string) {
        sb_print_string(b, v.(string))
    } else if type_of(v) == type_of(u8) {
        sb_print_int(b, int(v.(u8)))
    } else if type_of(v) == type_of(u16) {
        sb_print_int(b, int(v.(u16)))
    } else if type_of(v) == type_of(u32) {
        sb_print_int(b, int(v.(u32)))
    } else if type_of(v) == type_of(u64) {
        sb_print_int(b, int(v.(u64)))
    } else if type_of(v) == type_of(uintptr) {
        sb_print_int(b, int(v.(uintptr)))
    } else if type_of(v) == type_of(rune) {
        sb_print_int(b, int(v.(rune)))
    } else if type_of(v) == type_of(byte) {
        sb_print_byte(b, v.(byte))
    } else if type_of(v) == type_of(f64) {
        sb_print_f64(b, v.(f64))
    } else if type_of(v) == type_of(f32) {
        sb_print_f64(b, f64(v.(f32)))
    } else if type_of(v) == type_of(bool) {
        if v.(bool) {
            sb_print_string(b, "true")
        } else {
            sb_print_string(b, "false")
        }
    } else {
        sb_print_string(b, "<?>")
    }
}

sb_print_int :: proc(b: ^strings.Builder, v: int) {
    s := int_to_string(v)
    sb_print_string(b, s)
}

sb_print_f64 :: proc(b: ^strings.Builder, v: f64) {
    if v < 0 {
        sb_print_byte(b, '-')
        v = -v
    }
    int_part := int(v)
    s := int_to_string(int_part)
    sb_print_string(b, s)
    sb_print_byte(b, '.')
    frac_part := v - f64(int_part)
    for i in 0..<6 {
        frac_part *= 10.0
        digit := int(frac_part)
        sb_print_byte(b, u8('0' + digit))
        frac_part -= f64(digit)
    }
}

sb_print_hex :: proc(b: ^strings.Builder, v: int) {
    hex_digits := "0123456789abcdef"
    is_neg := v < 0
    if is_neg {
        sb_print_byte(b, '-')
        v = -v
    }
    if v >= 16 {
        sb_print_hex(b, v / 16)
    }
    sb_print_byte(b, hex_digits[v % 16])
}

sb_print_hex_upper :: proc(b: ^strings.Builder, v: int) {
    hex_digits := "0123456789ABCDEF"
    is_neg := v < 0
    if is_neg {
        sb_print_byte(b, '-')
        v = -v
    }
    if v >= 16 {
        sb_print_hex_upper(b, v / 16)
    }
    sb_print_byte(b, hex_digits[v % 16])
}

sb_print_binary :: proc(b: ^strings.Builder, v: int) {
    if v == 0 {
        sb_print_byte(b, '0')
        return
    }
    if v < 0 {
        sb_print_byte(b, '-')
        v = -v
    }
    bits: [64]u8
    i := 0
    for v > 0 {
        bits[i] = u8('0' + (v & 1))
        v = v >> 1
        i += 1
    }
    for i > 0 {
        i -= 1
        sb_print_byte(b, bits[i])
    }
}

sb_print_octal :: proc(b: ^strings.Builder, v: int) {
    if v == 0 {
        sb_print_byte(b, '0')
        return
    }
    if v < 0 {
        sb_print_byte(b, '-')
        v = -v
    }
    digits: [22]u8
    i := 0
    for v > 0 {
        digits[i] = u8('0' + (v & 7))
        v = v >> 3
        i += 1
    }
    for i > 0 {
        i -= 1
        sb_print_byte(b, digits[i])
    }
}

// --- strings.Builder format output ---

sbprint :: proc(b: ^strings.Builder, args: ..any) -> int {
    start := b.count
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(b, " ")
        }
        sb_print_value(b, args[i])
    }
    return b.count - start
}

sbprintf :: proc(b: ^strings.Builder, format: string, args: ..any) -> int {
    start := b.count
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        sb_print_string(b, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_hex_upper(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_binary(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_octal(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    sb_print_byte(b, '%')
                }
            }
        } else {
            sb_print_byte(b, format[i])
        }
        i += 1
    }
    return b.count - start
}

sbprintln :: proc(b: ^strings.Builder, args: ..any) -> int {
    start := b.count
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(b, " ")
        }
        sb_print_value(b, args[i])
    }
    sb_print_string(b, "\n")
    return b.count - start
}

sbprintfln :: proc(b: ^strings.Builder, format: string, args: ..any) -> int {
    start := b.count
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i < len(format) {
                spec := format[i]
                if spec == 'd' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 's' {
                    if arg_idx < len(args) {
                        s := args[arg_idx].(string)
                        sb_print_string(b, s)
                    }
                    arg_idx += 1
                } else if spec == 'x' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'X' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_hex_upper(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'u' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'b' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_binary(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'o' {
                    if arg_idx < len(args) {
                        v := args[arg_idx]
                        if type_of(v) == type_of(int) {
                            sb_print_octal(b, v.(int))
                        } else {
                            sb_print_value(b, args[arg_idx])
                        }
                    }
                    arg_idx += 1
                } else if spec == 'f' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == 'v' {
                    if arg_idx < len(args) {
                        sb_print_value(b, args[arg_idx])
                    }
                    arg_idx += 1
                } else if spec == '%' {
                    sb_print_byte(b, '%')
                }
            }
        } else {
            sb_print_byte(b, format[i])
        }
        i += 1
    }
    sb_print_string(b, "\n")
    return b.count - start
}