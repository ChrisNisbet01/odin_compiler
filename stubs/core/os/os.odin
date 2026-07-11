package os

args: []string

exit :: proc(code: int) {
    os_exit(code)
}
