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
