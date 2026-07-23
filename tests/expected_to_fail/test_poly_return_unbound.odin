package test

import "core:os"

// $T appears only in return position — without explicit type
// annotation on the caller side, $T is unbound and cannot be inferred.
zero_value :: proc() -> $T {
	return 0
}

main :: proc() {
	// This MUST fail: no surrounding context provides a type to bind $T
	// (the := syntax derives type from the call, but the call has no
	// type yet — a chicken-and-egg problem).
	r := zero_value()
	os.exit(r)
}
