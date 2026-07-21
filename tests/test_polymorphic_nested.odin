package test

import "core:os"

identity :: proc(x: $T) -> T {
    return x
}

double :: proc(x: $T) -> T {
    // Calls another polymorphic proc (identity) from within a poly instantiation
    return identity(x) + identity(x)
}

main :: proc()
{
    result := double(21)
    // double(21) expands to identity(21) + identity(21) = 21 + 21 = 42
    if result == 42 do os.exit(0)
    os.exit(1)
}
