package strings

Builder :: struct {
    data: []byte,
    count: int,
}

builder_init :: proc(b: ^Builder, alloc: context.allocator) {
    b.data = nil
    b.count = 0
}

to_string :: proc(b: ^Builder, alloc: context.allocator) -> string {
    return string(b.data[:b.count])
}

write_byte :: proc(b: ^Builder, byte: u8) {
    if b.count >= len(b.data) {
        new_cap := len(b.data) * 2
        if new_cap == 0 {
            new_cap = 32
        }
        new_data := make([]byte, new_cap, alloc)
        copy(new_data, b.data)
        b.data = new_data
    }
    b.data[b.count] = byte
    b.count += 1
}

write_string :: proc(b: ^Builder, s: string) {
    needed := len(s)
    if b.count + needed > len(b.data) {
        new_cap := len(b.data) + needed
        if new_cap == 0 {
            new_cap = 32
        }
        new_data := make([]byte, new_cap, context.allocator)
        copy(new_data, b.data)
        b.data = new_data
    }
    copy(b.data[b.count:], s)
    b.count += needed
}

write_rune :: proc(b: ^Builder, r: rune) {
    buf: [4]byte
    w := utf8.encode_rune(buf[:], r)
    write_string(b, string(buf[:w]))
}