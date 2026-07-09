package main
import "core:os"

main :: proc() {
    result := 0
    when true {
        result = 99
    }
    when false {
        result = result + 1
    }
    os.exit(result - 99)
}
