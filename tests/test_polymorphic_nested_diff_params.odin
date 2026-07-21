package test

import "core:os"

first :: proc(x: $T) -> T {
    return x
}

second :: proc(x: $U) -> U {
    return x
}

apply :: proc(x: $T) -> T {
    // Call two different poly procs with different param names
    a := first(x)
    b := second(x)
    return a + b
}

main :: proc()
{
    result := apply(30)
    // apply(30) => first(30) + second(30) = 30 + 30 = 60
    if result == 60 do os.exit(0)
    os.exit(1)
}
