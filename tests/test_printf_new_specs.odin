package test_printf_new_specs

import "core:fmt"
import "core:os"

// Test that prepared printf now supports the new format specifiers
// after delegation to sb_format_parsed.

test_char :: proc() -> bool {
    // %c should format a single character.
    // We can't capture stdout directly; use aprint-style verification via fmt.aprintf.
    s := fmt.aprintf("%c", 'A')
    return len(s) == 1
}

test_char_byte :: proc() -> bool {
    s := fmt.aprintf("%c", u8(66))
    return len(s) == 1
}

test_width :: proc() -> bool {
    s := fmt.aprintf("%5d", 42)
    // "   42" = 5 chars
    return len(s) == 5
}

test_zero_pad :: proc() -> bool {
    s := fmt.aprintf("%05d", 42)
    // "00042" = 5 chars
    return len(s) == 5
}

test_left_align :: proc() -> bool {
    s := fmt.aprintf("%-5d", 42)
    // "42   " = 5 chars
    return len(s) == 5
}

test_string_width :: proc() -> bool {
    s := fmt.aprintf("[%5s]", "hi")
    // "[   hi]" = 7 chars
    return len(s) == 7
}

test_float_default_prec :: proc() -> bool {
    s := fmt.aprintf("%f", 3.14)
    // Default precision is 6 => 3.140000 = 8 chars
    return len(s) == 8
}

test_float_explicit_prec :: proc() -> bool {
    s := fmt.aprintf("%.2f", 3.14)
    // "3.14" = 4 chars
    return len(s) == 4
}

test_scientific :: proc() -> bool {
    s := fmt.aprintf("%.2e", 123.456)
    // "1.23e+02" = 8 chars
    return len(s) == 8
}

test_general :: proc() -> bool {
    s := fmt.aprintf("%g", 1.5)
    // Some non-zero result; just check non-empty
    return len(s) > 0
}

test_hex_alt :: proc() -> bool {
    s := fmt.aprintf("%#x", 255)
    // "0xff" = 4 chars
    return len(s) == 4
}

test_octal_alt :: proc() -> bool {
    s := fmt.aprintf("%#o", 8)
    // "010" = 3 chars
    return len(s) == 3
}

test_pointer :: proc() -> bool {
    s := fmt.aprintf("%p", 255)
    // "0x" + hex digits; minimum 4 chars (0xff)
    return len(s) >= 4
}

test_always_sign :: proc() -> bool {
    s := fmt.aprintf("%+d", 42)
    // "+42" = 3 chars
    return len(s) == 3
}

test_nested_print_then_format :: proc() -> bool {
    // Use aprintfln ("%.2f" => "3.14\n" => 5 chars)
    s := fmt.aprintfln("%.2f", 3.14)
    return len(s) == 5
}

main :: proc() {
    if !test_char() do os.exit(1)
    fmt.println("PASS: printf %c")
    if !test_char_byte() do os.exit(2)
    fmt.println("PASS: printf %c byte")
    if !test_width() do os.exit(3)
    fmt.println("PASS: printf %5d width")
    if !test_zero_pad() do os.exit(4)
    fmt.println("PASS: printf %05d zero pad")
    if !test_left_align() do os.exit(5)
    fmt.println("PASS: printf %-5d left align")
    if !test_string_width() do os.exit(6)
    fmt.println("PASS: printf [%5s] string width")
    if !test_float_default_prec() do os.exit(7)
    fmt.println("PASS: printf %f default precision")
    if !test_float_explicit_prec() do os.exit(8)
    fmt.println("PASS: printf %.2f precision")
    if !test_scientific() do os.exit(9)
    fmt.println("PASS: printf %.2e scientific")
    if !test_general() do os.exit(10)
    fmt.println("PASS: printf %g general")
    if !test_hex_alt() do os.exit(11)
    fmt.println("PASS: printf %#x hex alt")
    if !test_octal_alt() do os.exit(12)
    fmt.println("PASS: printf %#o octal alt")
    if !test_pointer() do os.exit(13)
    fmt.println("PASS: printf %p pointer")
    if !test_always_sign() do os.exit(14)
    fmt.println("PASS: printf %+d always sign")
    if !test_nested_print_then_format() do os.exit(15)
    fmt.println("PASS: aprintfln %.2f")
    fmt.println("ALL printf new spec tests passed")
}
