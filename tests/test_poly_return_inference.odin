package test

import "core:os"
import "core:fmt"

// $T appears ONLY in the return position — no parameter binds it.
// The caller must provide the type via an explicit var decl.
zero_value :: proc() -> $T {
	return 0
}

// $T appears in BOTH param and return position — the parameter binds $T
// through args, but the return path also uses it.
identity :: proc(x: $T) -> T {
	return x
}

// $T appears only in return position; uses a constant returned value.
one_value :: proc() -> $T {
	return 1
}

main :: proc() {
	// (1) int specialization via explicit type annotation
	a: int = zero_value()
	fmt.printf("int zero: %d\n", a)
	if a != 0 do os.exit(1)

	// (2) i64 specialization
	b: i64 = zero_value()
	if b != 0 do os.exit(2)

	// (3) u32 specialization
	c: u32 = zero_value()
	if c != 0 do os.exit(3)

	// (4) `one_value` with int specialization returns 1
	d: int = one_value()
	fmt.printf("int one: %d\n", d)
	if d != 1 do os.exit(4)

	// (5) Mixed: regular poly call (param-binding path) still works
	e: int = identity(42)
	fmt.printf("identity(42): %d\n", e)
	if e != 42 do os.exit(5)

	os.exit(0)
}
