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
    // Missing Blue case — no #partial, no default — should error
    }
    os.exit(result - 2)
}
