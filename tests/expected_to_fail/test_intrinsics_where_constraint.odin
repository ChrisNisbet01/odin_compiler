package main

fdot :: proc(a: $T, b: $T) -> T
    where intrinsics.type_is_float($T)
{
    return a + b
}

main :: proc() {
    x: int = fdot(2, 3)
    if x != 5 { os.exit(1) }
    os.exit(0)
}
