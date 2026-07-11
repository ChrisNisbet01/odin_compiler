package main

import "core:os"

main :: proc() {
    count := len(os.args)
    if count != 1 {
        os.exit(count)
    }
    os.exit(0)
}
