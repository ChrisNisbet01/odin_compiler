package main
import "core:os"

main :: proc() {
    // String .len and .data
    s: string = "hello"
    l: int = s.len
    d: ^u8 = s.data
    if l != 5 { os.exit(1) }
    if d == nil { os.exit(2) }

    // Array .len
    arr: [3]int
    arr[0] = 10
    arr[1] = 20
    arr[2] = 30
    n: int = arr.len
    if n != 3 { os.exit(3) }

    // Maybe(T).value
    m: Maybe(int) = 42
    v: int = m.value
    if v != 42 { os.exit(4) }

    // Chained member access: struct.field.subfield
    loc: struct { file: string; line: int; column: int }
    loc.file = "test.odin"
    loc.line = 7
    loc.column = 5
    if loc.line != 7 { os.exit(5) }
    if loc.column != 5 { os.exit(6) }
    file_len: int = loc.file.len
    if file_len != 9 { os.exit(7) }
    if loc.file.data == nil { os.exit(8) }

    // Pointer auto-dereference: p.field
    p: ^struct { file: string; line: int; column: int } = &loc
    if p.line != 7 { os.exit(9) }
    if p.column != 5 { os.exit(10) }
    if p.file.len != 9 { os.exit(11) }

    os.exit(0)
}
