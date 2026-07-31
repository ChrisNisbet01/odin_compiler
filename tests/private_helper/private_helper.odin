package private_helper

@(private)
helper_secret :: proc(x: int) -> int {
    return x + 1
}

helper_public :: proc(x: int) -> int {
    return x + 1
}

@private
PRIVATE_CONST :: 42

PUBLIC_CONST :: 7

uses_private_const :: proc() -> int {
    return PRIVATE_CONST
}
