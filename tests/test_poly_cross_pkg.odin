package main

import "test_poly_cross_pkg_helper"
import "core:os"
import "core:fmt"

main :: proc() {
	// Package-qualified poly call (int)
	r1 := test_poly_cross_pkg_helper.identity(42)
	fmt.printf("identity(42): %d\n", r1)
	if r1 != 42 do os.exit(1)

	// Package-qualified poly call (float)
	r2 := test_poly_cross_pkg_helper.identity(3.14)
	fmt.printf("identity(3.14): %g\n", r2)
	if r2 != 3.14 do os.exit(2)

	// Package-qualified poly call with where clause
	r3 := test_poly_cross_pkg_helper.identity_int_only(7)
	fmt.printf("identity_int_only(7): %d\n", r3)
	if r3 != 7 do os.exit(3)

	// Package-qualified poly with two params
	r4 := test_poly_cross_pkg_helper.add(10, 20)
	fmt.printf("add(10,20): %d\n", r4)
	if r4 != 30 do os.exit(4)

	// Package-qualified poly with size_of where clause
	r6 := test_poly_cross_pkg_helper.sum_same_size(100, 200)
	fmt.printf("sum_same_size: %d\n", r6)
	if r6 != 300 do os.exit(6)

	// Non-poly cross-package baseline
	r5 := test_poly_cross_pkg_helper.helper_int(41)
	fmt.printf("helper_int(41): %d\n", r5)
	if r5 != 42 do os.exit(5)

	os.exit(0)
}
