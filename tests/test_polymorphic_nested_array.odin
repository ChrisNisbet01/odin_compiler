package test

import "core:os"

sum_array :: proc(arr: [$N]int) -> int
{
    total := 0
    for i in 0..<N do total += arr[i]
    return total
}

wrap_sum :: proc(arr: [$N]int) -> int
{
    // Call another poly proc (sum_array) from within a poly instantiation
    return sum_array(arr)
}

main :: proc()
{
    a: [3]int
    a[0] = 10; a[1] = 20; a[2] = 30

    result := wrap_sum(a)
    // wrap_sum(a) => sum_array(a) => 60
    if result != 60 do os.exit(1)
    os.exit(0)
}
