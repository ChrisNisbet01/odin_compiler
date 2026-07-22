package main

import "test_poly_cross_pkg_helper_fail"
import "core:os"

main :: proc() {
	// This should fail because typeid_of(f64) != typeid_of(int) (where
	// clause mismatch). Cross-package where clauses should enforce.
	r := test_poly_cross_pkg_helper_fail.identity_int_only(3.14)
	os.exit(0)
}


