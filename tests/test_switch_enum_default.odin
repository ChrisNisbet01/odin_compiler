package main
import "core:os"

Color :: enum { Red, Green, Blue }

main :: proc() {
    c: Color
    c = Green
    result := 0
    switch c {
    case Red:
        result = 1
    case Green:
        result = 2
    case:
        result = 99
    // Missing Blue case — default clause suppresses exhaustiveness error
    }
    os.exit(result - 2)
}
