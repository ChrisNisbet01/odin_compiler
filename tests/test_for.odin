package main
import "core:os"

main :: proc() {
    sum: int = 0
    i: int = 0
    for i < 10 {
        sum = sum + i
        i = i + 1
    }
    os.exit(sum - sum)
}
