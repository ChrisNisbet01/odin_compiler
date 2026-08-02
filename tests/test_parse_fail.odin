package main

import "core:os"

@(require_results)
identity_array_based_matrix :: proc "contextless" ($T: typeid/[$N][N]$E) -> (m: T) #no_bounds_check {
	for i in 0..<N {
		m[i][i] = E(1)
	}
	return m
}

main :: proc() {
    os.exit(0)
}
