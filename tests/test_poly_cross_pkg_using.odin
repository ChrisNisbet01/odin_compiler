package main

import using "test_poly_cross_pkg_helper"
import "core:os"
import "core:fmt"

main :: proc() {
	// `using`-imported poly call (unqualified)
	r1 := identity(42)
	fmt.printf("identity(42): %d\n", r1)
	if r1 != 42 do os.exit(1)

	// `using`-imported poly call with where clause
	r3 := identity_int_only(7)
	fmt.printf("identity_int_only(7): %d\n", r3)
	if r3 != 7 do os.exit(3)

	// `using`-imported poly with two params
	r4 := add(10, 20)
	if r4 != 30 do os.exit(4)

	// `using`-imported poly with size_of where clause
	r6 := sum_same_size(100, 200)
	if r6 != 300 do os.exit(6)

	os.exit(0)
}
