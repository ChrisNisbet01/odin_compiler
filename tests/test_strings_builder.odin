package test_strings_builder

import "core:strings"
import "core:fmt"
import "core:os"

main :: proc() {
    test_builder_make()
    test_write_byte()
    test_write_string()
    test_to_string()
    test_write_mixed()
    fmt.println("ALL strings.Builder tests passed")
}

test_builder_make :: proc() {
    b := strings.builder_make_none()
    if strings.builder_cap(b) != 0 do os.exit(1)
    if strings.builder_space(b) != 0 do os.exit(2)
    fmt.println("PASS: builder_make_none")
}

test_write_byte :: proc() {
    b := strings.builder_make_none()
    n := strings.write_byte(&b, 'H')
    if n != 1 do os.exit(10)
    n = strings.write_byte(&b, 'i')
    if n != 1 do os.exit(11)
    if b.count != 2 do os.exit(12)
    fmt.println("PASS: write_byte")
}

test_write_string :: proc() {
    b := strings.builder_make_none()
    n := strings.write_string(&b, "hello")
    if n != 5 do os.exit(20)
    if b.count != 5 do os.exit(21)
    n = strings.write_string(&b, " world")
    if n != 6 do os.exit(22)
    if b.count != 11 do os.exit(23)
    fmt.println("PASS: write_string")
}

test_to_string :: proc() {
    b := strings.builder_make_none()
    strings.write_string(&b, "hello")
    s := strings.to_string(b)
    if len(s) != 5 do os.exit(30)
    if s[0] != 'h' do os.exit(31)
    if s[4] != 'o' do os.exit(32)
    fmt.println("PASS: to_string")
}

test_write_mixed :: proc() {
    b := strings.builder_make_none()
    strings.write_string(&b, "abc")
    strings.write_byte(&b, 'd')
    strings.write_string(&b, "ef")
    s := strings.to_string(b)
    if len(s) != 6 do os.exit(40)
    if s[0] != 'a' do os.exit(41)
    if s[3] != 'd' do os.exit(42)
    if s[5] != 'f' do os.exit(43)
    fmt.println("PASS: write_mixed")
}
