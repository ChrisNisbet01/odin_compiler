package main
import "core:os"

main :: proc() {
    a := 5
    b := 10
    c := min(a, b)
    d := max(a, b)
    result: int = 0
    if c == 5 { result = result + 1 }
    if d == 10 { result = result + 1 }
    os.exit(result - 2)
}
