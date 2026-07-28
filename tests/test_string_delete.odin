package main
import "core:os"

main :: proc() {
    result := 0

    // Test: delete on a dynamic array (data ptr == alloc start so free() works)
    arr := make([dynamic]int, 0, 10)
    delete(arr)

    // Test: delete on a slice
    sl := make([]int, 5)
    if len(sl) != 5 {
        result = 1
    }
    delete(sl)

    os.exit(result)
}
