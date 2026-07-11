package main

import "core:os"
import "core:fmt"

main :: proc() {
    for i in 1..<len(os.args) {
        fmt.println(os.args[i])
    }
}
