package main

import "core:os"
import "base:intrinsics"

@private IS_FLOAT :: intrinsics.type_is_float

@(require_results)
vector_normalize0 :: proc "contextless" (v: $T/[$N]$E) -> T where IS_FLOAT(E) {
	m := length(v)
	return 0 if m == 0 else v/m
}

main :: proc() {
    os.exit(0)
}
