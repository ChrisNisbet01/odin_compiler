package main
import "core:os"

helper :: proc() -> (int, string) {
    return 42, "hello"
}

main :: proc() {
    result := 0

    // 1. Direct tuple type declaration
    a: [int, string]

    // 2. Type alias for tuple
    MyTuple :: [int, string]
    b: MyTuple

    // 3. Multi-return destructuring (regression test)
    c, d := helper()
    if c != 42 {
        result = result + 1
    }
    if len(d) != 5 {
        result = result + 1
    }

    os.exit(result)
}
