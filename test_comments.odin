// This is a line comment
package main

/* Block comment
   across lines */
import "core:fmt"

main :: proc() {
    // inline comment
    fmt.println("hi") /* trailing */
}