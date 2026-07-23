package test
import "core:os"
import "core:fmt"

first :: proc(s: []int) -> int {
	return s[0]
}

sum_slice :: proc(s: []int) -> int {
	total := 0
	for i in 0..<len(s) {
		total += s[i]
	}
	return total
}

main :: proc() {
	// Test 1: make() + pass to function
	s1: []int = make([]int, 3)
	s1[0] = 10
	s1[1] = 20
	s1[2] = 30
	r1 := first(s1)
	fmt.printf("make first: %d\n", r1)
	if r1 != 10 do os.exit(1)

	// Test 2: make() + sum via function
	r2 := sum_slice(s1)
	fmt.printf("make sum: %d\n", r2)
	if r2 != 60 do os.exit(2)

	// Test 3: arr[:] + pass to function
	arr: [3]int
	arr[0] = 100
	arr[1] = 200
	arr[2] = 300
	s2 := arr[:]
	r3 := first(s2)
	fmt.printf("fullslice first: %d\n", r3)
	if r3 != 100 do os.exit(3)

	// Test 4: arr[:] + sum via function
	r4 := sum_slice(s2)
	fmt.printf("fullslice sum: %d\n", r4)
	if r4 != 600 do os.exit(4)

	// Test 5: arr[:] + direct access (regression check)
	if s2[2] != 300 do os.exit(5)

	// Test 6: make() + direct access (regression check)
	if s1[1] != 20 do os.exit(6)

	fmt.printf("all slice passing tests passed\n")
	os.exit(0)
}
