package runtime

@(builtin)
print_string :: proc(fd: int, str: string) ---
@(builtin)
print_byte   :: proc(fd: int, b: u8) ---
@(builtin)
int_to_string :: proc(i: int) -> string ---
@(builtin)
os_exit      :: proc(code: int) ---
@(builtin)
sys_open     :: proc(path: string, flags: int, mode: int) -> int ---
@(builtin)
sys_read     :: proc(fd: int, data: ^u8, count: int) -> int ---
@(builtin)
sys_write    :: proc(fd: int, data: ^u8, count: int) -> int ---
@(builtin)
sys_close    :: proc(fd: int) -> int ---

@(builtin)
mem_alloc :: proc(size: int, alignment: int, allocator: Allocator) -> (data: rawptr, err: Allocator_Error) ---

@(builtin)
mem_free :: proc(mem: rawptr, allocator: Allocator) ---

@(builtin)
copy :: proc(dst: rawptr, src: rawptr, n: int) ---

@(builtin)
align_forward_uint :: proc(x: uint, alignment: uint) -> uint ---

@(builtin)
mem_zero :: proc(ptr: rawptr, size: int) ---

@(builtin)
any_type_id :: proc(v: any) -> i64 ---

@(builtin)
any_data_ptr :: proc(v: any) -> rawptr ---

@(builtin)
type_info_lookup :: proc(type_id: i64) -> rawptr ---

@(builtin)
array_element :: proc(arr: any, index: int) -> any ---

@(builtin)
matrix_element :: proc(m: any, row: int, col: int) -> any ---