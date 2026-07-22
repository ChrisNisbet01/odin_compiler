package test

import "core:os"
import "core:fmt"

identity :: proc(x: $T) -> T ---
identity :: proc(x: $T) -> T {
	return x
}

add :: proc(a: $T, b: $T) -> T ---
add :: proc(a: $T, b: $T) -> T {
	return a + b
}

swap_values :: proc(a: $T, b: $T) -> (T, T) ---
swap_values :: proc(a: $T, b: $T) -> (T, T) {
	return b, a
}

identity_int_only :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) ---
identity_int_only :: proc(x: $T) -> T where typeid_of(T) == typeid_of(int) {
	return x
}

main :: proc() {
	r1 := identity(42)
	fmt.printf("identity(42): %d\n", r1)
	if r1 != 42 do os.exit(1)

	r2 := identity(3.14)
	fmt.printf("identity(3.14): %g\n", r2)
	if r2 != 3.14 do os.exit(2)

	r3 := add(10, 20)
	fmt.printf("add(10,20): %d\n", r3)
	if r3 != 30 do os.exit(3)

	x, y := swap_values(100, 200)
	fmt.printf("swap: %d %d\n", x, y)
	if x != 200 do os.exit(4)
	if y != 100 do os.exit(5)

	f1, f2 := swap_values(1.5, 2.5)
	fmt.printf("swap f: %g %g\n", f1, f2)

	r4 := identity_int_only(7)
	fmt.printf("identity_int_only(7): %d\n", r4)
	if r4 != 7 do os.exit(6)

	os.exit(0)
}
