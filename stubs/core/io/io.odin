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

write_byte :: proc(w: ^Writer, x: byte) -> (n: int, err: Error) ---
write_string :: proc(w: ^Writer, s: string) -> (n: int, err: Error) ---
write_rune :: proc(w: ^Writer, r: rune) -> (n: int, err: Error) ---
flush :: proc(w: ^Writer) -> Error ---