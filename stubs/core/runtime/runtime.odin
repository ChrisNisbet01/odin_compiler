package runtime

print_string :: proc(fd: int, str: string) ---
print_byte   :: proc(fd: int, b: u8) ---
int_to_string :: proc(i: int) -> string ---
os_exit      :: proc(code: int) ---
sys_open     :: proc(path: string, flags: int, mode: int) -> int ---
sys_read     :: proc(fd: int, data: ^u8, count: int) -> int ---
sys_write    :: proc(fd: int, data: ^u8, count: int) -> int ---
sys_close    :: proc(fd: int) -> int ---
