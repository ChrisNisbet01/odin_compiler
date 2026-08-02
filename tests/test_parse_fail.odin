package main

import "core:os"
import "base:intrinsics"

@(require_results)
vector_length_a :: proc "contextless" (v: $T/[$N]$E) -> E where IS_FLOAT(E) {
	return #force_inline math.sqrt(dot(v, v))
}

@(require_results)
vector_length_b :: proc "contextless" (v: $T/[$N]$E) -> E where IS_FLOAT(E) {
	return #force_inline math.sqrt(#force_inline dot(v, v))
}

main :: proc() {
    os.exit(0)
}
