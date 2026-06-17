package main

main :: proc() -> int {
    v: struct { inner: struct { x: int; y: int }; z: int }
    v.inner.x = 10
    v.inner.y = 20
    v.z = 30

    deep: struct { a: struct { b: struct { c: int } } }
    deep.a.b.c = 99

    return v.inner.x + v.inner.y + v.z + deep.a.b.c - 159
}
