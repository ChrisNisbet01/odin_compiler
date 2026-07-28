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

write :: proc(w: Writer, p: []byte) -> Error {
	if w.procedure != nil {
		_, err := w.procedure(w.data, .Write, p, 0, .Start)
		return err
	}
	return Unsupported
}

write_byte :: proc(w: Writer, c: byte) -> Error {
	buf: [1]byte
	buf[0] = c
	_, err := w.procedure(w.data, .Write, buf[:], 0, .Start)
	return err
}

write_string :: proc(w: Writer, str: string) -> Error {
	_, err := w.procedure(w.data, .Write, transmute([]byte)str, 0, .Start)
	return err
}

write_rune :: proc(w: Writer, r: rune) -> Error {
	if r < 0x80 {
		return write_byte(w, byte(r))
	}
	buf: [4]byte
	buf[0] = byte(r >> 18 | 0xC0)
	buf[1] = byte(r >> 12 & 0x3F | 0x80)
	buf[2] = byte(r >> 6 & 0x3F | 0x80)
	buf[3] = byte(r & 0x3F | 0x80)
	_, err := w.procedure(w.data, .Write, buf[:], 0, .Start)
	return err
}

flush :: proc(w: Writer) -> Error {
	if w.procedure != nil {
		_, err := w.procedure(w.data, .Flush, nil[:], 0, .Start)
		return err
	}
	return Error.None
}