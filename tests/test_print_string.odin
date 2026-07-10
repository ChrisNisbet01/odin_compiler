package main
import "core:os"

main :: proc() {
    print_string(1, "Hello, World!")
    os.exit(0)
}
