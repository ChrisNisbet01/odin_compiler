package main

import "test_poly_cross_pkg_helper"
import "core:os"
import "core:fmt"

main :: proc() {
	// Cross-package overload bundle with one poly + one non-poly candidate.
	// `identity_int` wins for int via exact match (non-poly adds 100).
	r1 := test_poly_cross_pkg_helper.mixed_bundle(42)
	fmt.printf("mixed_bundle(42): %d\n", r1)
	if r1 != 142 do os.exit(1)

	// For float, identity_int fails, identity_any specializes with $T=f64
	r2 := test_poly_cross_pkg_helper.mixed_bundle(3.14)
	fmt.printf("mixed_bundle(3.14): %g\n", r2)
	if r2 != 3.14 do os.exit(2)

	os.exit(0)
}
