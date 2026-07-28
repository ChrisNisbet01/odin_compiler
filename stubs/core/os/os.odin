package os

args: []string

O_RDONLY :: 0
O_WRONLY :: 1
O_RDWR   :: 2
O_CREAT  :: 64
O_TRUNC  :: 512
O_APPEND :: 1024

__odin_open_handles: [256]int
__odin_num_handles: int = 0

__odin_track_handle :: proc(fd: int) {
    if __odin_num_handles < 256 {
        __odin_open_handles[__odin_num_handles] = fd
        __odin_num_handles += 1
    }
}

__odin_untrack_handle :: proc(fd: int) {
    for i in 0..<__odin_num_handles {
        if __odin_open_handles[i] == fd {
            for j in i..<__odin_num_handles - 1 {
                __odin_open_handles[j] = __odin_open_handles[j + 1]
            }
            __odin_num_handles -= 1
            return
        }
    }
}

__odin_close_all_handles :: proc() {
    for i in 0..<__odin_num_handles {
        sys_close(__odin_open_handles[i])
    }
    __odin_num_handles = 0
}

open :: proc(path: string, flags: int, mode: int) -> (int, int) {
    fd := sys_open(path, flags, mode)
    if fd < 0 {
        return -1, -fd
    }
    __odin_track_handle(fd)
    return fd, 0
}

close :: proc(fd: int) -> int {
    __odin_untrack_handle(fd)
    result := sys_close(fd)
    if result < 0 {
        return -result
    }
    return 0
}

read :: proc(fd: int, data: ^u8, count: int) -> (int, int) {
    result := sys_read(fd, data, count)
    if result < 0 {
        return 0, -result
    }
    return result, 0
}

write :: proc(fd: int, data: ^u8, count: int) -> (int, int) {
    result := sys_write(fd, data, count)
    if result < 0 {
        return 0, -result
    }
    return result, 0
}

exit :: proc(code: int) {
    __odin_close_all_handles()
    os_exit(code)
}
