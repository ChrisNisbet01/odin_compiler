package strings

Builder :: struct {
    buf:   [dynamic]byte;
    count: int;
}

builder_make_none :: proc() -> Builder {
    return Builder{}
}

builder_make :: proc(n: int, allocator := context.allocator) -> Builder {
    b: Builder;
    b.buf = make([dynamic]byte, n, allocator);
    return b;
}

builder_make_temp :: proc(n: int) -> Builder {
    b: Builder;
    b.buf = make([dynamic]byte, n, context.temp_allocator);
    return b;
}

builder_cap :: proc(b: Builder) -> int {
    return cap(b.buf);
}

builder_space :: proc(b: Builder) -> int {
    return cap(b.buf) - b.count;
}

@(builtin)
to_string :: proc(b: Builder) -> string ---
@(builtin)
to_bytes :: proc(b: Builder) -> []byte ---

write_byte :: proc(b: ^Builder, x: byte) -> int {
    b.buf = append(b.buf, x);
    b.count += 1;
    return 1;
}

write_bytes :: proc(b: ^Builder, x: []byte) -> int {
    for i in 0..<len(x) {
        b.buf = append(b.buf, x[i]);
    }
    b.count += len(x);
    return len(x);
}

write_string :: proc(b: ^Builder, s: string) -> int {
    for i in 0..<len(s) {
        b.buf = append(b.buf, s[i]);
    }
    b.count += len(s);
    return len(s);
}

reset :: proc(b: ^Builder) {
    b.count = 0;
}

grow :: proc(b: ^Builder, n: int) {
    needed := b.count + n;
    if needed > cap(b.buf) {
        new_cap := cap(b.buf);
        if new_cap == 0 {
            new_cap = 4;
        }
        for new_cap < needed {
            new_cap = new_cap * 2;
        }
        new_buf := make([dynamic]byte, new_cap);
        for i in 0..<b.count {
            new_buf = append(new_buf, b.buf[i]);
        }
        b.buf = new_buf;
    }
}

destroy :: proc(b: ^Builder) {
    delete(b.buf);
    b.count = 0;
}
