package io

Error :: enum {
    None,
    EOF,
    Closed,
    InvalidArgument,
    NoPermission,
    NotFound,
    TooManyOpenFiles,
}

Writer :: struct {
    procedure: proc(stream_data: rawptr, mode: Stream_Mode, p: []byte, offset: i64, whence: Seek_From) -> (n: i64, err: Error),
    data: rawptr,
}

Stream_Mode :: enum {
    Read,
    Write,
}

Seek_From :: enum {
    Start,
    Current,
    End,
}

write_byte :: proc(w: Writer, b: u8, n_written: ^int) {
    buf: [1]u8 = {b}
    n, err := w.procedure(w.data, Stream_Mode.Write, buf[:], 0, Seek_From.Start)
    if n_written != nil {
        n_written^ = int(n)
    }
}

write_string :: proc(w: Writer, s: string, n_written: ^int) {
    buf := []byte(s)
    n, err := w.procedure(w.data, Stream_Mode.Write, buf, 0, Seek_From.Start)
    if n_written != nil {
        n_written^ = int(n)
    }
    _ = err
}

flush :: proc(w: Writer) -> Error {
    return Error.None
}