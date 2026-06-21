package main

main :: proc() -> int {
    result: int = 0

    // Test make with map
    m := make(map[int]int, 16)
    if len(m) != 0 {
        result = result + 1
    }
    if cap(m) != 16 {
        result = result + 2
    }

    // Test map subscript write (insert)
    m[10] = 42
    if len(m) != 1 {
        result = result + 4
    }

    // Test map subscript read (lookup)
    v := m[10]
    if v != 42 {
        result = result + 8
    }

    // Test map subscript overwrite (update existing key)
    m[10] = 99
    v2 := m[10]
    if v2 != 99 {
        result = result + 16
    }

    // Test map subscript read for missing key (returns zero)
    v3 := m[999]
    if v3 != 0 {
        result = result + 32
    }

    // Test multiple distinct keys
    // Keys so far: 10->99, now adding 1, 2, 3 = 4 total entries
    m[1] = 100
    m[2] = 200
    m[3] = 300
    if len(m) != 4 {
        result = result + 64
    }

    // Verify values
    if m[1] != 100 {
        result = result + 128
    }
    if m[2] != 200 {
        result = result + 256
    }
    if m[3] != 300 {
        result = result + 512
    }
    if m[10] != 99 {
        result = result + 1024
    }

    // Test delete on map
    delete(m)
    if len(m) != 0 {
        // After delete, data is freed; len is undefined
        // Just checking delete doesn't crash
    }

    return result
}
