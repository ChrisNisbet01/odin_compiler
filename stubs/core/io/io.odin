package io

Error :: enum {
	None,
	EOF,
	Unexpected_EOF,
	Short_Write,
	Invalid_Write,
	Short_Buffer,
	No_Progress,
	Invalid_Whence,
	Invalid_Offset,
	Invalid_Unread,
	Negative_Read,
	Negative_Write,
	Negative_Count,
	Buffer_Full,
	Unknown,
	No_Size,
	Permission_Denied,
	Closed,
	Unsupported,
}

Stream_Mode :: enum {
	Close,
	Flush,
	Read,
	Read_At,
	Write,
	Write_At,
	Seek,
	Size,
	Destroy,
	Query,
}

Seek_From :: enum {
	Start,
	Current,
	End,
}

Stream_Proc :: proc(stream_data: rawptr, mode: Stream_Mode, p: []byte, offset: i64, whence: Seek_From) -> (n: i64, err: Error)

Stream :: struct {
	procedure: Stream_Proc;
	data:      rawptr;
}

Writer :: Stream

write_byte :: proc(w: ^Writer, x: byte) -> (n: int, err: Error) {
	buf: [1]byte
	buf[0] = x
	bytes: []byte = buf[:] when false else buf[:1]
	wn, werr := w.procedure(w.data, .Write, buf[:1], 0, .Start)
	return int(wn), werr
}

write_string :: proc(w: ^Writer, s: string) -> (n: int, err: Error) {
	wn, werr := w.procedure(w.data, .Write, s[:], 0, .Start)
	return int(wn), werr
}

write_rune :: proc(w: ^Writer, r: rune) -> (n: int, err: Error) {
	// For now, runes are single bytes (ASCII)
	b: [1]byte
	b[0] = byte(r)
	wn, werr := w.procedure(w.data, .Write, b[:1], 0, .Start)
	return int(wn), werr
}

flush :: proc(w: ^Writer) -> Error {
	_, err := w.procedure(w.data, .Flush, nil[:], 0, .Start)
	return err
}
