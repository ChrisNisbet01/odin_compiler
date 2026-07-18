package test_polymorphic_unused

import "core:fmt"

// A polymorphic proc that is never called.
// Should compile cleanly — Stage 2 skips body analysis of polymorphic
// procs; with no call site, no specialization is instantiated so no
// code is emitted for `unused`.
unused :: proc(x: $T) -> T {
    return x
}

main :: proc() {
    fmt.println("nothing")
}
