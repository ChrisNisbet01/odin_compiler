package main

foreign libc {
    strlen :: proc (s: ptr u8) -> size_t ---
    strcpy :: proc (dest: ptr u8, src: ptr u8) -> _ --- 
    strncmp :: proc (s1: ptr u8, s2: ptr u8, n: size_t) -> int ---
    printf :: proc (fmt: ptr u8, ...) -> i32 ---
}

main :: proc() -> int {
    // Test simple string
    str1: string = "hello"
    len1 := strlen(str1)
    if len1 != 5 {
        return 1
    }
    
    // Test string assignment
    str2: string
    strcpy(str2, str1)
    if str2 != "hello" {
        return 2
    }
    
    // Test compare
    if strncmp(str1, "hell", 4) != 0 {
        return 3
    }
    
    // Test printf
    result := printf("Test string: %s\n", str1)
    if result <= 0 {
        return 4
    }
    
    return 0
}