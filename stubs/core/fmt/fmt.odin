package fmt

import "core:strings"
import "core:io"

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
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    s := strings.to_string(b)
    print_string(1, s)
}

printfln :: proc(format: string, args: ..any) {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    print_string(1, s)
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
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    s := strings.to_string(b)
    print_string(2, s)
}

eprintfln :: proc(format: string, args: ..any) {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    print_string(2, s)
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
    } else if type_of(v) == type_of(cstring) {
        cs := v.(cstring)
        addr := uintptr(cs)
        for {
            p := cast(^u8)(addr)
            b := p^
            if b == 0 do break
            print_byte(fd, b)
            addr += 1
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
    } else if type_of(v) == type_of(cstring) {
        cs := v.(cstring)
        addr := uintptr(cs)
        for {
            p := cast(^u8)(addr)
            ch := p^
            if ch == 0 do break
            sb_print_byte(b, ch)
            addr += 1
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
    return sb_format_parsed(b, format, args)
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

// --- Helper: f64 raw formatting (no width padding, no temp builder) ---

sb_print_f64_raw :: proc(b: ^strings.Builder, v: f64, precision: int) {
    is_neg := v < 0
    if is_neg {
        sb_print_byte(b, '-')
        v = -v
    }
    int_part := int(v)
    s := int_to_string(int_part)
    sb_print_string(b, s)
    if precision > 0 {
        sb_print_byte(b, '.')
        frac_part := v - f64(int_part)
        for i in 0..<precision {
            frac_part *= 10.0
            digit := int(frac_part)
            sb_print_byte(b, u8('0' + digit))
            frac_part -= f64(digit)
        }
    }
}

// --- Helper: f64 format with width and precision ---

sb_format_f64 :: proc(b: ^strings.Builder, v: any, width: int, precision: int, flags: int) {
    val: f64
    if type_of(v) == type_of(f64) {
        val = v.(f64)
    } else if type_of(v) == type_of(f32) {
        val = f64(v.(f32))
    } else if type_of(v) == type_of(int) {
        val = f64(v.(int))
    } else {
        val = 0.0
    }

    is_neg := val < 0
    if is_neg {
        sb_print_byte(b, '-')
        val = -val
    } else if flags & FLAG_ALWAYS_SIGN != 0 {
        sb_print_byte(b, '+')
    } else if flags & FLAG_SPACE_SIGN != 0 {
        sb_print_byte(b, ' ')
    }

    int_part := int(val)
    s := int_to_string(int_part)
    sb_print_string(b, s)
    if precision > 0 {
        sb_print_byte(b, '.')
        frac_part := val - f64(int_part)
        for i in 0..<precision {
            frac_part *= 10.0
            digit := int(frac_part)
            sb_print_byte(b, u8('0' + digit))
            frac_part -= f64(digit)
        }
    }

    content_len := b.count

    pad_total := 0
    if width > content_len {
        pad_total = width - content_len
    }

    if pad_total > 0 && flags & FLAG_LEFT_ALIGN != 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
}

// --- Helper: scientific notation ---

sb_format_scientific :: proc(b: ^strings.Builder, v: any, upper: bool, width: int, precision: int, flags: int) {
    val: f64
    if type_of(v) == type_of(f64) {
        val = v.(f64)
    } else if type_of(v) == type_of(f32) {
        val = f64(v.(f32))
    } else if type_of(v) == type_of(int) {
        val = f64(v.(int))
    } else {
        val = 0.0
    }

    is_neg := val < 0
    if is_neg {
        sb_print_byte(b, '-')
        val = -val
    } else if flags & FLAG_ALWAYS_SIGN != 0 {
        sb_print_byte(b, '+')
    } else if flags & FLAG_SPACE_SIGN != 0 {
        sb_print_byte(b, ' ')
    }

    if val == 0 {
        sb_print_byte(b, '0')
        if precision > 0 {
            sb_print_byte(b, '.')
            for i in 0..<precision {
                sb_print_byte(b, '0')
            }
        }
        if upper {
            sb_print_string(b, "E+00")
        } else {
            sb_print_string(b, "e+00")
        }
        return
    }

    // Compute exponent
    exp := 0
    if val >= 10.0 {
        for val >= 10.0 {
            val /= 10.0
            exp += 1
        }
    } else if val < 1.0 {
        for val < 1.0 {
            val *= 10.0
            exp -= 1
        }
    }

    // Mantissa: one digit before decimal, precision digits after
    mant_digit := int(val)
    sb_print_byte(b, u8('0' + mant_digit))
    val -= f64(mant_digit)
    if precision > 0 {
        sb_print_byte(b, '.')
        for i in 0..<precision {
            val *= 10.0
            d := int(val)
            sb_print_byte(b, u8('0' + d))
            val -= f64(d)
        }
    }

    // Exponent
    if upper {
        sb_print_string(b, "E")
    } else {
        sb_print_string(b, "e")
    }
    if exp >= 0 {
        sb_print_byte(b, '+')
    } else {
        sb_print_byte(b, '-')
        exp = -exp
    }
    if exp < 10 {
        sb_print_byte(b, '0')
    }
    sb_print_int(b, exp)
}

// --- Helper: general format (%g / %G) ---

sb_format_general :: proc(b: ^strings.Builder, v: any, upper: bool, width: int, precision: int, flags: int) {
    val: f64
    if type_of(v) == type_of(f64) {
        val = v.(f64)
    } else if type_of(v) == type_of(f32) {
        val = f64(v.(f32))
    } else if type_of(v) == type_of(int) {
        val = f64(v.(int))
    } else {
        val = 0.0
    }

    is_neg := val < 0
    abs_val := val
    if is_neg {
        abs_val = -val
    }

    if abs_val == 0 {
        sb_print_byte(b, '0')
        return
    }

    exp := 0
    tmp := abs_val
    if tmp >= 10.0 {
        for tmp >= 10.0 {
            tmp /= 10.0
            exp += 1
        }
    } else if tmp < 1.0 {
        for tmp < 1.0 {
            tmp *= 10.0
            exp -= 1
        }
    }

    // Use %e if exponent < -4 or >= precision, otherwise %f
    if exp < -4 || exp >= precision {
        sb_format_scientific(b, v, upper, 0, precision - 1, flags)
    } else {
        frac_digits := precision - 1 - exp
        if frac_digits < 0 {
            frac_digits = 0
        }
        sb_format_f64(b, v, 0, frac_digits, flags)
    }
}
// --- Helper: hex lower with zero padding ---

sb_print_hex_lower_padded :: proc(b: ^strings.Builder, v: int, min_digits: int, width: int, flags: int) {
    hex_digits := "0123456789abcdef"
    digits_buf: [16]u8
    digit_count := 0
    val := v
    if val == 0 {
        digits_buf[0] = '0'
        digit_count = 1
    } else {
        for val > 0 && digit_count < 16 {
            digits_buf[digit_count] = hex_digits[val & 15]
            val >>= 4
            digit_count += 1
        }
    }
    pad_zeros := 0
    if min_digits > digit_count {
        pad_zeros = min_digits - digit_count
    }
    content_len := pad_zeros + digit_count
    pad_total := 0
    if width > content_len {
        pad_total = width - content_len
    }
    if flags & FLAG_LEFT_ALIGN == 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
    if pad_zeros > 0 {
        sb_print_repeat(b, '0', pad_zeros)
    }
    for i in 0..<digit_count {
        sb_print_byte(b, digits_buf[digit_count - 1 - i])
    }
    if flags & FLAG_LEFT_ALIGN != 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
}

// --- Helper: integer formatting with base/width/precision/flags ---

sb_format_int :: proc(b: ^strings.Builder, v: any, base: int, upper: bool, width: int, precision: int, flags: int, is_unsigned: bool) {
    val := 0
    is_neg := false

    if is_unsigned {
        if type_of(v) == type_of(u8) {
            val = int(v.(u8))
        } else if type_of(v) == type_of(u16) {
            val = int(v.(u16))
        } else if type_of(v) == type_of(u32) {
            val = int(v.(u32))
        } else if type_of(v) == type_of(u64) {
            val = int(v.(u64))
        } else if type_of(v) == type_of(uintptr) {
            val = int(v.(uintptr))
        } else if type_of(v) == type_of(byte) {
            val = int(v.(byte))
        } else {
            val = 0
        }
    } else {
        if type_of(v) == type_of(int) {
            val = v.(int)
        } else if type_of(v) == type_of(i8) {
            val = int(v.(i8))
        } else if type_of(v) == type_of(i16) {
            val = int(v.(i16))
        } else if type_of(v) == type_of(i32) {
            val = int(v.(i32))
        } else if type_of(v) == type_of(i64) {
            val = int(v.(i64))
        } else if type_of(v) == type_of(u8) {
            val = int(v.(u8))
        } else if type_of(v) == type_of(u16) {
            val = int(v.(u16))
        } else if type_of(v) == type_of(u32) {
            val = int(v.(u32))
        } else if type_of(v) == type_of(u64) {
            val = int(v.(u64))
        } else if type_of(v) == type_of(uintptr) {
            val = int(v.(uintptr))
        } else if type_of(v) == type_of(byte) {
            val = int(v.(byte))
        } else {
            val = 0
        }
        if !is_unsigned && val < 0 {
            is_neg = true
            val = -val
        }
    }

    // Convert to string in given base (use temp, preserving val for prefix check)
    digits_buf: [65]u8
    digit_count := 0
    tmp_val := val
    if tmp_val == 0 {
        digits_buf[0] = '0'
        digit_count = 1
    } else {
        for tmp_val > 0 && digit_count < 65 {
            d := tmp_val % base
            if upper {
                if d < 10 {
                    digits_buf[digit_count] = u8('0' + d)
                } else {
                    digits_buf[digit_count] = u8('A' + d - 10)
                }
            } else {
                if d < 10 {
                    digits_buf[digit_count] = u8('0' + d)
                } else {
                    digits_buf[digit_count] = u8('a' + d - 10)
                }
            }
            tmp_val = tmp_val / base
            digit_count += 1
        }
    }

    // Build prefix
    prefix_len := 0
    prefix_buf: [4]u8
    if is_neg {
        prefix_buf[0] = '-'
        prefix_len = 1
    } else if flags & FLAG_ALWAYS_SIGN != 0 {
        prefix_buf[0] = '+'
        prefix_len = 1
    } else if flags & FLAG_SPACE_SIGN != 0 {
        prefix_buf[0] = ' '
        prefix_len = 1
    }
    if flags & FLAG_ALTERNATE != 0 && val != 0 {
        if base == 16 {
            prefix_buf[prefix_len] = '0'
            if upper { prefix_buf[prefix_len + 1] = 'X' } else { prefix_buf[prefix_len + 1] = 'x' }
            prefix_len += 2
        } else if base == 8 {
            prefix_buf[prefix_len] = '0'
            prefix_len += 1
        } else if base == 2 {
            prefix_buf[prefix_len] = '0'
            if upper { prefix_buf[prefix_len + 1] = 'B' } else { prefix_buf[prefix_len + 1] = 'b' }
            prefix_len += 2
        }
    }

    // Precision pads digits (minimum digits)
    pad_zeros := 0
    if precision > 0 && precision > digit_count {
        pad_zeros = precision - digit_count
    }

    // Total content length
    content_len := prefix_len + pad_zeros + digit_count

    // Width padding
    pad_total := 0
    if width > content_len {
        pad_total = width - content_len
    }

    if flags & FLAG_ZERO_PAD != 0 && flags & FLAG_LEFT_ALIGN == 0 && precision < 0 {
        pad_zeros = pad_total
        pad_total = 0
    }

    // Left padding
    if flags & FLAG_LEFT_ALIGN == 0 {
        sb_print_repeat(b, ' ', pad_total)
    }

    // Prefix
    for pi in 0..<prefix_len {
        sb_print_byte(b, prefix_buf[pi])
    }

    // Precision zeros
    if pad_zeros > 0 {
        sb_print_repeat(b, '0', pad_zeros)
    }

    // Digits (reversed)
    for di in 0..<digit_count {
        sb_print_byte(b, digits_buf[digit_count - 1 - di])
    }

    // Right padding
    if flags & FLAG_LEFT_ALIGN != 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
}
// --- Format flag constants ---
FLAG_LEFT_ALIGN  :: 1
FLAG_ALWAYS_SIGN :: 2
FLAG_SPACE_SIGN  :: 4
FLAG_ZERO_PAD    :: 8
FLAG_ALTERNATE   :: 16

sb_format_parsed_inner :: proc(b: ^strings.Builder, format: string, args: []any) -> int {
    start := b.count
    arg_idx := 0
    i := 0
    for i < len(format) {
        if format[i] == '%' {
            i += 1
            if i >= len(format) do break

            // Parse flags
            flags := 0
            for i < len(format) {
                if format[i] == '-' {
                    flags |= FLAG_LEFT_ALIGN
                    i += 1
                } else if format[i] == '+' {
                    flags |= FLAG_ALWAYS_SIGN
                    i += 1
                } else if format[i] == ' ' {
                    flags |= FLAG_SPACE_SIGN
                    i += 1
                } else if format[i] == '0' {
                    flags |= FLAG_ZERO_PAD
                    i += 1
                } else if format[i] == '#' {
                    flags |= FLAG_ALTERNATE
                    i += 1
                } else {
                    break
                }
            }
            if i >= len(format) do break

            // Parse width
            width := 0
            has_width := false
            for i < len(format) && format[i] >= '0' && format[i] <= '9' {
                width = width * 10 + int(format[i] - '0')
                has_width = true
                i += 1
            }
            if i >= len(format) do break

            // Parse precision
            precision := -1
            if i < len(format) && format[i] == '.' {
                i += 1
                precision = 0
                for i < len(format) && format[i] >= '0' && format[i] <= '9' {
                    precision = precision * 10 + int(format[i] - '0')
                    i += 1
                }
            }
            if i >= len(format) do break

            // Parse spec
            spec := format[i]
            i += 1

            if spec == 'd' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 10, false, width, precision, flags, false)
                }
                arg_idx += 1
            } else if spec == 's' {
                if arg_idx < len(args) {
                    s := args[arg_idx].(string)
                    sb_print_padded_string(b, s, width, flags)
                }
                arg_idx += 1
            } else if spec == 'x' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 16, false, width, precision, flags, false)
                }
                arg_idx += 1
            } else if spec == 'X' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 16, true, width, precision, flags, false)
                }
                arg_idx += 1
            } else if spec == 'u' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 10, false, width, precision, flags, true)
                }
                arg_idx += 1
            } else if spec == 'b' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 2, false, width, precision, flags, false)
                }
                arg_idx += 1
            } else if spec == 'o' {
                if arg_idx < len(args) {
                    sb_format_int(b, args[arg_idx], 8, false, width, precision, flags, false)
                }
                arg_idx += 1
            } else if spec == 'c' {
                if arg_idx < len(args) {
                    v := args[arg_idx]
                    c := byte(0)
                    if type_of(v) == type_of(byte) {
                        c = v.(byte)
                    } else if type_of(v) == type_of(u8) {
                        c = v.(u8)
                    } else if type_of(v) == type_of(int) {
                        c = u8(v.(int))
                    } else if type_of(v) == type_of(rune) {
                        c = u8(v.(rune))
                    }
                    sb_print_padded_char(b, c, width, flags)
                }
                arg_idx += 1
            } else if spec == 'f' || spec == 'F' {
                if arg_idx < len(args) {
                    prec := precision
                    if prec < 0 {
                        prec = 6
                    }
                    sb_format_f64(b, args[arg_idx], width, prec, flags)
                }
                arg_idx += 1
            } else if spec == 'e' || spec == 'E' {
                if arg_idx < len(args) {
                    prec := precision
                    if prec < 0 {
                        prec = 6
                    }
                    upper := spec == 'E'
                    sb_format_scientific(b, args[arg_idx], upper, width, prec, flags)
                }
                arg_idx += 1
            } else if spec == 'g' || spec == 'G' {
                if arg_idx < len(args) {
                    prec := precision
                    if prec < 0 {
                        prec = 6
                    }
                    upper := spec == 'G'
                    sb_format_general(b, args[arg_idx], upper, width, prec, flags)
                }
                arg_idx += 1
            } else if spec == 'p' {
                if arg_idx < len(args) {
                    v := args[arg_idx]
                    ptr_val := 0
                    if type_of(v) == type_of(int) {
                        ptr_val = v.(int)
                    } else if type_of(v) == type_of(uintptr) {
                        ptr_val = int(v.(uintptr))
                    } else if type_of(v) == type_of(u64) {
                        ptr_val = int(v.(u64))
                    }
                    sb_print_string(b, "0x")
                    sb_print_hex_lower_padded(b, ptr_val, 0, width, flags)
                }
                arg_idx += 1
            } else if spec == 'v' {
                if arg_idx < len(args) {
                    sb_print_value(b, args[arg_idx])
                }
                arg_idx += 1
            } else if spec == '%' {
                sb_print_byte(b, '%')
            } else {
                // Unknown spec: print literally
                sb_print_byte(b, '%')
                sb_print_byte(b, spec)
            }
        } else {
            sb_print_byte(b, format[i])
            i += 1
        }
    }
    return b.count - start
}

// --- Core format parser: parses flags/width/precision/spec and dispatches ---

sb_format_parsed :: proc(b: ^strings.Builder, format: string, args: ..any) -> int {
    return sb_format_parsed_inner(b, format, args)
}

// --- Helper: repeat character ---

sb_print_repeat :: proc(b: ^strings.Builder, c: byte, count: int) {
    for i in 0..<count {
        sb_print_byte(b, c)
    }
}

// --- Helper: padded char ---

sb_print_padded_char :: proc(b: ^strings.Builder, c: byte, width: int, flags: int) {
    pad_total := 0
    if width > 1 {
        pad_total = width - 1
    }
    if flags & FLAG_LEFT_ALIGN == 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
    sb_print_byte(b, c)
    if flags & FLAG_LEFT_ALIGN != 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
}

// --- Helper: padded string ---

sb_print_padded_string :: proc(b: ^strings.Builder, s: string, width: int, flags: int) {
    pad_total := 0
    if width > len(s) {
        pad_total = width - len(s)
    }
    if flags & FLAG_LEFT_ALIGN == 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
    sb_print_string(b, s)
    if flags & FLAG_LEFT_ALIGN != 0 {
        sb_print_repeat(b, ' ', pad_total)
    }
}

// --- Helper: flush builder to fd ---

flush_to_fd :: proc(b: ^strings.Builder, fd: int) {
    s := strings.to_string(b^)
    print_string(fd, s)
}

// --- Allocate-based print (returns allocated string) ---

aprint :: proc(args: ..any) -> string {
    b := strings.builder_make_none()
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    return strings.to_string(b)
}

aprintln :: proc(args: ..any) -> string {
    b := strings.builder_make_none()
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    sb_print_byte(&b, '\n')
    return strings.to_string(b)
}

aprintf :: proc(format: string, args: ..any) -> string {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    return strings.to_string(b)
}

aprintfln :: proc(format: string, args: ..any) -> string {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    return strings.to_string(b)
}
sbprintfln :: proc(b: ^strings.Builder, format: string, args: ..any) -> int {
    start := b.count
    sb_format_parsed(b, format, args)
    sb_print_string(b, "\n")
    return b.count - start
}

// --- Temp-allocator variants (use context.temp_allocator for builder buffer) ---

tprint :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(1, " ")
        }
        print_value(1, args[i])
    }
}

tprintln :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(1, " ")
        }
        print_value(1, args[i])
    }
    print_string(1, "\n")
}

tprintf :: proc(format: string, args: ..any) {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    s := strings.to_string(b)
    print_string(1, s)
}

tprintfln :: proc(format: string, args: ..any) {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    print_string(1, s)
}

teprintln :: proc(args: ..any) {
    for i in 0..<len(args) {
        if i > 0 {
            print_string(2, " ")
        }
        print_value(2, args[i])
    }
    print_string(2, "\n")
}

teprintf :: proc(format: string, args: ..any) {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    s := strings.to_string(b)
    print_string(2, s)
}

teprintfln :: proc(format: string, args: ..any) {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    print_string(2, s)
}

// --- Writer variants (write to io.Writer) ---

wprint :: proc(w: io.Writer, args: ..any) {
    b := strings.builder_make_none()
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    s := strings.to_string(b)
    io.write_string(w, s)
}

wprintln :: proc(w: io.Writer, args: ..any) {
    b := strings.builder_make_none()
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    io.write_string(w, s)
}

wprintf :: proc(w: io.Writer, format: string, args: ..any) {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    s := strings.to_string(b)
    io.write_string(w, s)
}

wprintfln :: proc(w: io.Writer, format: string, args: ..any) {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    s := strings.to_string(b)
    io.write_string(w, s)
}

// --- C string variants (return null-terminated cstring) ---
// These build the output in a string builder, append a null byte,
// then return cstring(raw_data(s)).
// Simplified versions: no sep/allocator/newline named params.

caprint :: proc(args: ..any) -> cstring {
    b := strings.builder_make_none()
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}

caprintf :: proc(format: string, args: ..any) -> cstring {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}

caprintfln :: proc(format: string, args: ..any) -> cstring {
    b := strings.builder_make_none()
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}

ctprint :: proc(args: ..any) -> cstring {
    b := strings.builder_make_temp(64)
    for i in 0..<len(args) {
        if i > 0 {
            sb_print_string(&b, " ")
        }
        sb_print_value(&b, args[i])
    }
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}

ctprintf :: proc(format: string, args: ..any) -> cstring {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}

ctprintfln :: proc(format: string, args: ..any) -> cstring {
    b := strings.builder_make_temp(64)
    sb_format_parsed(&b, format, args)
    sb_print_byte(&b, '\n')
    sb_print_byte(&b, 0)
    s := strings.to_string(b)
    return cstring(raw_data(s))
}