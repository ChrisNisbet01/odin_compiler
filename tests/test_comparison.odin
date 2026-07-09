package main
import "core:os"

main :: proc() {
    x: int = 5
    y: int = 10
    lt: int = x < y
    os.exit(lt - lt)
}
