package main

import "core:os"

// Compile-time `when N == X` selection inside a polymorphic proc.
// Only the branch matching the specialized $N is analysed and codegen'd.
pick :: proc(a: $T/[$N]$E) -> E {
    when N == 1 {
        return a[0]
    } else when N == 2 {
        return a[0] + a[1]
    } else when N == 3 {
        return a[0] + a[1] + a[2]
    } else {
        c: E
        c = a[0] + a[N - 1]
        return c
    }
}

main :: proc() {
    a1 := [1]int{7}
    a2 := [2]int{3, 4}
    a3 := [3]int{1, 2, 3}
    a4 := [4]int{1, 2, 3, 4}

    if pick(a1) != 7 {
        os.exit(1)
    }
    if pick(a2) != 7 {
        os.exit(2)
    }
    if pick(a3) != 6 {
        os.exit(3)
    }
    if pick(a4) != 5 {
        os.exit(4)
    }

    os.exit(0)
}
