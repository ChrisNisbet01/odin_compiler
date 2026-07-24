package test

import "core:os"
import "core:fmt"

Vec :: struct {
	x: int;
	y: int;
}

Box :: struct($T: typeid) {
	val: T;
}

Triple :: struct {
	a: int;
	b: int;
	c: int;
}

print_vec :: proc(v: Vec) {
	fmt.printf("vec(%d,%d)\n", v.x, v.y)
}

main :: proc() {
	result := 0

	// 1. Regular struct literal - init form
	v1 := Vec{x = 1, y = 2}
	if v1.x != 1 do os.exit(1)
	if v1.y != 2 do os.exit(2)

	// 2. Regular struct literal - assignment form
	v2: Vec
	v2 = Vec{x = 10, y = 20}
	if v2.x != 10 do os.exit(3)
	if v2.y != 20 do os.exit(4)

	// 3. Poly struct literal - int
	b1 := Box(int){val = 42}
	if b1.val != 42 do os.exit(5)

	// 4. Poly struct literal - f64
	b2 := Box(f64){val = 3.14}
	if b2.val != 3.14 do os.exit(6)

	// 5. Poly struct literal - assignment form
	b3: Box(int)
	b3 = Box(int){val = 99}
	if b3.val != 99 do os.exit(7)

	// 6. Struct literal with 3 fields
	t := Triple{a = 10, b = 20, c = 30}
	if t.a != 10 do os.exit(8)
	if t.b != 20 do os.exit(9)
	if t.c != 30 do os.exit(10)

	// 7. Struct literal as function argument
	print_vec(Vec{x = 7, y = 8})

	// 8. Struct literal field access after construction
	v8 := Vec{x = 100, y = 200}
	sum := v8.x + v8.y
	if sum != 300 do os.exit(11)

	fmt.println("struct literal tests OK")
	os.exit(result)
}
