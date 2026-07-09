package private_helper

@(private)
helper_secret :: proc(x: int) -> int {
    return x + 1
}

helper_public :: proc(x: int) -> int {
    return x + 1
}
