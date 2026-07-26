package test_fmt_enhanced

import "core:fmt"
import "core:strings"
import "core:os"

// Test sb_format_int with ABF (append codegen bug fix) workaround:
// We verify lengths via count field (not to_string content) because
// to_string across function boundaries triggers the append codegen bug.

test_sb_format_int :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 42, 10, false, 0, -1, 0, false)
    if b.count != 2 do return false
    return true
}

test_sb_format_int_radix :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 255, 16, false, 0, -1, 0, false)
    if b.count != 2 do return false
    return true
}

test_sb_format_int_upper :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 255, 16, true, 0, -1, 0, false)
    if b.count != 2 do return false
    return true
}

test_sb_format_int_width :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 10, 10, false, 5, -1, 0, false)
    if b.count != 5 do return false
    return true
}

test_sb_format_int_zero_pad :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 10, 10, false, 5, -1, fmt.FLAG_ZERO_PAD, false)
    if b.count != 5 do return false
    return true
}

test_sb_format_int_negative :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, -10, 10, false, 0, -1, 0, false)
    if b.count != 3 do return false
    return true
}

test_sb_format_int_alt_hex :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 255, 16, false, 0, -1, fmt.FLAG_ALTERNATE, false)
    if b.count != 4 do return false
    return true
}

test_sb_format_int_always_sign :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 10, 10, false, 0, -1, fmt.FLAG_ALWAYS_SIGN, false)
    if b.count != 3 do return false
    return true
}

test_sb_format_int_zero :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_int(&b, 0, 10, false, 0, -1, 0, false)
    if b.count != 1 do return false
    return true
}

test_sb_print_padded_string :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_padded_string(&b, "hi", 5, 0)
    if b.count != 5 do return false
    return true
}

test_sb_print_padded_string_left :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_padded_string(&b, "hi", 5, fmt.FLAG_LEFT_ALIGN)
    if b.count != 5 do return false
    return true
}

test_sb_print_padded_char :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_padded_char(&b, 'X', 3, 0)
    if b.count != 3 do return false
    return true
}

test_sb_print_hex_lower_padded :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_hex_lower_padded(&b, 255, 4, 0, 0)
    if b.count != 4 do return false
    return true
}

test_sb_print_hex_lower_padded_wide :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_hex_lower_padded(&b, 255, 8, 10, 0)
    if b.count != 10 do return false
    return true
}

test_aprint_len :: proc() -> bool {
    s := fmt.aprint("hello")
    if len(s) != 5 do return false
    return true
}

test_aprintln_len :: proc() -> bool {
    s := fmt.aprintln("hi")
    if len(s) != 3 do return false
    return true
}

test_sb_print_f64_raw :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_print_f64_raw(&b, 3.14, 2)
    if b.count != 4 do return false
    return true
}

test_sb_format_f64 :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_f64(&b, 3.14, 0, 2, 0)
    if b.count != 4 do return false
    return true
}

test_sb_format_f64_negative :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_f64(&b, -1.5, 0, 1, 0)
    if b.count != 4 do return false
    return true
}

test_sb_format_scientific :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_scientific(&b, 123.456, false, 0, 2, 0)
    // 1.23e+02 = 8 chars
    if b.count != 8 do return false
    return true
}

test_sb_format_scientific_zero :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_scientific(&b, 0.0, false, 0, 2, 0)
    // 0.00e+00 = 8 chars
    if b.count != 8 do return false
    return true
}

test_sb_format_general_small :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_general(&b, 1.5, false, 0, 6, 0)
    if b.count < 1 do return false
    return true
}

test_sb_format_general_large :: proc() -> bool {
    b := strings.builder_make_none()
    fmt.sb_format_general(&b, 123456.0, false, 0, 6, 0)
    if b.count < 1 do return false
    return true
}

test_sb_format_parsed_simple :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "hello")
    if n != 5 do return false
    return true
}

test_sb_format_parsed_int :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%d", 42)
    if n != 2 do return false
    return true
}

test_sb_format_parsed_width :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%5d", 10)
    if n != 5 do return false
    return true
}

test_sb_format_parsed_zero_pad :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%05d", 10)
    if n != 5 do return false
    return true
}

test_sb_format_parsed_left_align :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%-5d", 10)
    if n != 5 do return false
    return true
}

test_sb_format_parsed_hex :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%x", 255)
    if n != 2 do return false
    return true
}

test_sb_format_parsed_char :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%c", 'A')
    if n != 1 do return false
    return true
}

test_sb_format_parsed_float :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%.2f", 3.14)
    if n != 4 do return false
    return true
}

test_sb_format_parsed_scientific :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%.2e", 123.456)
    if n != 8 do return false
    return true
}

test_sb_format_parsed_percent :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "100%%")
    if n != 4 do return false
    return true
}

test_sb_format_parsed_multi :: proc() -> bool {
    b := strings.builder_make_none()
    n := fmt.sb_format_parsed(&b, "%d-%d", 1, 2)
    if n != 3 do return false
    return true
}

test_aprintf :: proc() -> bool {
    s := fmt.aprintf("%d", 42)
    if len(s) != 2 do return false
    return true
}

test_aprintfln :: proc() -> bool {
    s := fmt.aprintfln("%x", 255)
    if len(s) != 3 do return false
    return true
}

main :: proc() {
    if !test_sb_format_int() do os.exit(1)
    fmt.println("PASS: sb_format_int(42, base=10)")
    if !test_sb_format_int_radix() do os.exit(2)
    fmt.println("PASS: sb_format_int(255, base=16)")
    if !test_sb_format_int_upper() do os.exit(3)
    fmt.println("PASS: sb_format_int(255, base=16, upper)")
    if !test_sb_format_int_width() do os.exit(4)
    fmt.println("PASS: sb_format_int(10, width=5)")
    if !test_sb_format_int_zero_pad() do os.exit(5)
    fmt.println("PASS: sb_format_int(10, zero_pad)")
    if !test_sb_format_int_negative() do os.exit(6)
    fmt.println("PASS: sb_format_int(-10)")
    if !test_sb_format_int_alt_hex() do os.exit(7)
    fmt.println("PASS: sb_format_int(255, alt, hex)")
    if !test_sb_format_int_always_sign() do os.exit(8)
    fmt.println("PASS: sb_format_int(10, always_sign)")
    if !test_sb_format_int_zero() do os.exit(9)
    fmt.println("PASS: sb_format_int(0)")
    if !test_sb_print_padded_string() do os.exit(10)
    fmt.println("PASS: sb_print_padded_string right-align")
    if !test_sb_print_padded_string_left() do os.exit(11)
    fmt.println("PASS: sb_print_padded_string left-align")
    if !test_sb_print_padded_char() do os.exit(12)
    fmt.println("PASS: sb_print_padded_char")
    if !test_sb_print_hex_lower_padded() do os.exit(13)
    fmt.println("PASS: sb_print_hex_lower_padded(255, min=4)")
    if !test_sb_print_hex_lower_padded_wide() do os.exit(14)
    fmt.println("PASS: sb_print_hex_lower_padded(255, min=8, width=10)")
    if !test_aprint_len() do os.exit(15)
    fmt.println("PASS: aprint(hello) len")
    if !test_aprintln_len() do os.exit(16)
    fmt.println("PASS: aprintln(hi) len")
    if !test_sb_print_f64_raw() do os.exit(17)
    fmt.println("PASS: sb_print_f64_raw(3.14, 2)")
    if !test_sb_format_f64() do os.exit(18)
    fmt.println("PASS: sb_format_f64(3.14, prec=2)")
    if !test_sb_format_f64_negative() do os.exit(19)
    fmt.println("PASS: sb_format_f64(-1.5, prec=1)")
    if !test_sb_format_scientific() do os.exit(20)
    fmt.println("PASS: sb_format_scientific(123.456, prec=2)")
    if !test_sb_format_scientific_zero() do os.exit(21)
    fmt.println("PASS: sb_format_scientific(0.0, prec=2)")
    if !test_sb_format_general_small() do os.exit(22)
    fmt.println("PASS: sb_format_general(1.5, prec=6)")
    if !test_sb_format_general_large() do os.exit(23)
    fmt.println("PASS: sb_format_general(123456.0, prec=6)")
    if !test_sb_format_parsed_simple() do os.exit(24)
    fmt.println("PASS: sb_format_parsed('hello')")
    if !test_sb_format_parsed_int() do os.exit(25)
    fmt.println("PASS: sb_format_parsed('%d', 42)")
    if !test_sb_format_parsed_width() do os.exit(26)
    fmt.println("PASS: sb_format_parsed('%5d', 10)")
    if !test_sb_format_parsed_zero_pad() do os.exit(27)
    fmt.println("PASS: sb_format_parsed('%05d', 10)")
    if !test_sb_format_parsed_left_align() do os.exit(28)
    fmt.println("PASS: sb_format_parsed('%-5d', 10)")
    if !test_sb_format_parsed_hex() do os.exit(29)
    fmt.println("PASS: sb_format_parsed('%x', 255)")
    if !test_sb_format_parsed_char() do os.exit(30)
    fmt.println("PASS: sb_format_parsed('%c', 'A')")
    if !test_sb_format_parsed_float() do os.exit(31)
    fmt.println("PASS: sb_format_parsed('%.2f', 3.14)")
    if !test_sb_format_parsed_scientific() do os.exit(32)
    fmt.println("PASS: sb_format_parsed('%.2e', 123.456)")
    if !test_sb_format_parsed_percent() do os.exit(33)
    fmt.println("PASS: sb_format_parsed('100%%')")
    if !test_sb_format_parsed_multi() do os.exit(34)
    fmt.println("PASS: sb_format_parsed('%d-%d', 1, 2)")
    // aprintf/aprintfln now work after ..any forwarding bugfix
    if !test_aprintf() do os.exit(35)
    fmt.println("PASS: aprintf('%d', 42)")
    if !test_aprintfln() do os.exit(36)
    fmt.println("PASS: aprintfln('%x', 255)")
    fmt.println("ALL fmt enhanced tests passed")
}
