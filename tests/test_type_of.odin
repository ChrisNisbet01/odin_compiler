package main
import "core:os"

main :: proc() {
    x: i32
    y: f64
    id_x := type_of(x)
    id_y := type_of(y)

    // type_of returns the same id for the same type
    #assert[id_x == type_of(x)]

    // type_of returns different ids for different types
    #assert[id_x != id_y]

    // type_of works with typed expressions
    z: i32 = 42
    #assert[type_of(z) == id_x]

    os.exit(0)
}
