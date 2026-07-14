package main
import "core:os"

MyInt :: distinct int

main :: proc() {
    // distinct T where T is a type alias
    v: MyInt = 42
    os.exit(cast(int)v - 42)
}
