package test
import "core:os"
import "core:fmt"

// Poly array size + poly element type: [$N]$T
first :: proc(arr: [$N]$T) -> T {
	return arr[0]
}

// Two poly params with same size: [$N]$T, [$N]$U
sum_firsts :: proc(a: [$N]$T, b: [$N]$U) -> int {
	return int(a[0]) + int(b[0])
}

// Poly pointer: ^$T
deref_val :: proc(p: ^$T) -> T {
	return p^
}

main :: proc() {
	// Test 1: first with [3]int
	arr1: [3]int
	arr1[0] = 10
	arr1[1] = 20
	arr1[2] = 30
	r1: int = first(arr1)
	fmt.printf("first([3]int): %d\n", r1)
	if r1 != 10 do os.exit(1)

	// Test 2: first with [5]f64
	arr2: [5]f64
	arr2[0] = 7.5
	r2: f64 = first(arr2)
	fmt.printf("first([5]f64): %d\n", int(r2))
	if r2 < 7.49 || r2 > 7.51 do os.exit(2)

	// Test 3: sum_firsts with same-size arrays of different types
	a3: [3]int
	a3[0] = 100
	b3: [3]int
	b3[0] = 7
	r3: int = sum_firsts(a3, b3)
	fmt.printf("sum_firsts: %d\n", r3)
	if r3 != 107 do os.exit(3)

	// Test 4: first with [2]u8
	arr4: [2]u8
	arr4[0] = 42
	r4: u8 = first(arr4)
	fmt.printf("first([2]u8): %d\n", int(r4))
	if r4 != 42 do os.exit(4)

	// Test 5: deref_val with ^int
	x: int = 99
	r5: int = deref_val(&x)
	fmt.printf("deref_val: %d\n", r5)
	if r5 != 99 do os.exit(5)

	// Test 6: deref_val with ^f64
	y: f64 = 2.718
	r6: f64 = deref_val(&y)
	fmt.printf("deref_val(f64): %d\n", int(r6))
	if r6 < 2.71 || r6 > 2.72 do os.exit(6)

	fmt.printf("all nested type poly tests passed\n")
	os.exit(0)
}
