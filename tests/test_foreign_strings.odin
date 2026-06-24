package main

foreign libc {
    strlen :: proc "c" (s: ^u8) -> i32 ---
    strncmp :: proc "c" (s1: ^u8, s2: ^u8, n: i32) -> i32 ---
}

main :: proc() -> int {
    // Test strlen with a string literal
    n := strlen("hello")
    if n != 5 {
        return 1
    }

    // Test strlen with a string variable
    s: string = "world"
    n2 := strlen(s)
    if n2 != 5 {
        return 2
    }

    // Test strncmp with matching strings
    if strncmp("abc", "abc", 3) != 0 {
        return 3
    }

    // Test strncmp with differing strings
    if strncmp("abc", "abd", 3) == 0 {
        return 4
    }

    // Test prefix match via strncmp
    if strncmp("hello", "he", 2) != 0 {
        return 5
    }

    return 0
}
