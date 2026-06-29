package main

main :: proc() -> int {
    s: struct #soa { x: int; y: int; z: int }

    // Field access should return empty slices (len == 0)
    lx := len(s.x)
    ly := len(s.y)
    lz := len(s.z)

    total := lx + ly + lz
    return cast(int)total
}
