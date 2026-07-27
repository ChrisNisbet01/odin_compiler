package main

import "core:fmt"
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

    fmt.tprintln("count=", w.count)
    fmt.tprintln("len=", len(w.buf))
    fmt.tprintln("buf0=", int(w.buf[0]))
    fmt.tprintln("buf1=", int(w.buf[1]))
    fmt.tprintln("buf2=", int(w.buf[2]))
    os.exit(0)
}
