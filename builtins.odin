// print_string — wraps putchar for a string
print_string :: proc(s: string) {
    for i in 0..<len(s) {
        print_byte(s[i])
    }
}

print_byte :: proc(b: byte) {
    putchar(cast(int)b)
}

putchar :: proc "c" (c: int) -> int ---

// int_to_string — convert any integer to string
int_to_string :: proc(v: $T) -> string {
    // Only called when T is an integer type via print_value
    n := v
    if n == 0 {
        return "0"
    }
    neg := false
    if n < 0 {
        neg = true
        n = -n
    }
    buf: [21]byte
    pos := 20
    for n > 0 {
        pos -= 1
        buf[pos] = cast(byte)('0' + n % 10)
        n /= 10
    }
    if neg {
        pos -= 1
        buf[pos] = '-'
    }
    return string(buf[pos..21])
}
