package main

import "core:os"

// $N integer polymorphic parameter — array size inferred from argument
sum_array :: proc(arr: [$N]int) -> int
{
    total := 0
    for i in 0..<N do total += arr[i]
    return total
}

main :: proc()
{
    a: [5]int
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; a[4] = 5

    b: [3]int
    b[0] = 10; b[1] = 20; b[2] = 30

    r1 := sum_array(a)
    r2 := sum_array(b)

    if r1 != 15 do os.exit(1)
    if r2 != 60 do os.exit(2)

    os.exit(0)
}
