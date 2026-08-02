package main

import "core:os"

get_identity_rotation :: proc() -> quaternion128 {
    return quaternion(w = 1, x = 0, y = 0, z = 0)
}

main :: proc() {
    q := get_identity_rotation()
    if size_of(quaternion128) != 16 do os.exit(1)
    _ = q
    os.exit(0)
}
