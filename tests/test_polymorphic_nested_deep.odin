package test

import "core:os"

identity :: proc(x: $T) -> T {
    return x
}

double :: proc(x: $T) -> T {
    return identity(x) + identity(x)
}

triple :: proc(x: $T) -> T {
    return double(x) + identity(x)
}

main :: proc()
{
    // triple(10) => double(10) + identity(10) => (identity(10)+identity(10)) + 10 => 30
    result := triple(10)
    if result == 30 do os.exit(0)
    os.exit(1)
}
