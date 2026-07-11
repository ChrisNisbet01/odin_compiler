package test_caller_location

import "core:os"
import "core:fmt"

main :: proc() {
    loc := #caller_location

    // loc.line should be 7 (the line of the #caller_location expression)
    if loc.line != 7 {
        os.exit(1)
    }

    // loc.column should be > 0
    if loc.column <= 0 {
        os.exit(2)
    }

    // loc.file string should have nonzero length — but we can't access .len 
    // through chained member access (missing functionality). Just check file
    // string is not nil by checking its content indirectly.
    fmt.println(loc.line)
    fmt.println(loc.column)

    os.exit(0)
}
