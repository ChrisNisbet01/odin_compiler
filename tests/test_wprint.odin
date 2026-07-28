package main
import "core:fmt"
import "core:io"
import "core:os"

main :: proc() {
    result := 0
    w := os.stream_from_handle(os.stdout)

    // Test 1: wprint writes space-separated values
    fmt.wprint(w, "hello", 42)
    // Should output: hello 42

    // Test 2: wprintln adds a newline
    fmt.wprintln(w, "world")
    // Should output: world\n

    // Test 3: wprintf with format specifiers
    fmt.wprintf(w, "formatted: %d %s\n", 99, "test")
    // Should output: formatted: 99 test\n

    // Test 4: wprintfln with format specifiers + newline
    fmt.wprintfln(w, "fln: %v", 3.14)
    // Should output: fln: 3.140000\n

    // Test 5: write to stderr stream
    werr := os.stream_from_handle(os.stderr)
    fmt.wprintln(werr, "stderr test")

    os.exit(result)
}
