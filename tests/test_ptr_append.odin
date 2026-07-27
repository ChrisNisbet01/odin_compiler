package main

import "core:os"

Writer :: struct {
    buf:   [dynamic]byte;
    count: int;
}

write_byte :: proc(b: ^Writer, c: byte) {
    b.buf = append(b.buf, c)
    b.count += 1
}

main :: proc() {
    w: Writer
    w.buf = make([dynamic]byte, 0, 4)

    write_byte(&w, 'h')
    write_byte(&w, 'i')
    write_byte(&w, '!')

    if w.count != 3 { os.exit(1) }
    if w.buf[0] != 'h' { os.exit(2) }
    if w.buf[1] != 'i' { os.exit(3) }
    if w.buf[2] != '!' { os.exit(4) }
    os.exit(0)
}
