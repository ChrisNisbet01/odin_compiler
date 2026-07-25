package strings

Builder :: struct {
	buf: []byte;
	count: int;
}

builder_make_none :: proc() -> Builder ---
builder_make :: proc(n: int) -> Builder ---
builder_cap :: proc(b: Builder) -> int ---
builder_space :: proc(b: Builder) -> int ---
to_string :: proc(b: Builder) -> string ---
to_bytes :: proc(b: Builder) -> []byte ---
write_byte :: proc(b: ^Builder, x: byte) -> int ---
write_bytes :: proc(b: ^Builder, x: []byte) -> int ---
write_string :: proc(b: ^Builder, s: string) -> int ---
write_rune :: proc(b: ^Builder, r: rune) -> int ---
reset :: proc(b: ^Builder) ---
grow :: proc(b: ^Builder, n: int) ---