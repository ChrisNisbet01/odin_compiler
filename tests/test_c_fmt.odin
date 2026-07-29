package main
import "core:fmt"
import "core:os"

@(link_name="strlen")
strlen :: proc "c" (s: cstring) -> int ---

main :: proc() {
    result := 0

    // Test 1: ctprintf returns a cstring "hello 42" (8 chars)
    cs1 := fmt.ctprintf("hello %d", 42)
    if strlen(cs1) != 8 { result += 1 }

    // Test 2: ctprintfln returns "val: 3.140000\n" (14 chars, 6 default frac digits)
    cs2 := fmt.ctprintfln("val: %v", 3.14)
    if strlen(cs2) != 14 { result += 1 }

    // Test 3: ctprint returns "a b 123" (7 chars)
    cs3 := fmt.ctprint("a", "b", 123)
    if strlen(cs3) != 7 { result += 1 }

    // Test 4: caprintf returns "fmt: test 99" (12 chars)
    cs4 := fmt.caprintf("fmt: %s %d", "test", 99)
    if strlen(cs4) != 12 { result += 1 }

    // Test 5: caprintfln returns "newline: 7\n" (11 chars)
    cs5 := fmt.caprintfln("newline: %d", 7)
    if strlen(cs5) != 11 { result += 1 }

    // Test 6: caprint returns "1 2 3" (5 chars)
    cs6 := fmt.caprint(1, 2, 3)
    if strlen(cs6) != 5 { result += 1 }

    os.exit(result)
}
