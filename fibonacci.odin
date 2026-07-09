package fibonacci

import "core:fmt"

fib :: proc(v: u64) -> u64 {
    if v == 0 || v == 1 {
        return u64(1)
    }
    return u64(fib(v - 1) + fib(v - 2))
}

main :: proc() {
    run()
}

run :: proc() -> u64 {
    f: u64 = 6
    fib_value := fib(f)

    fmt.printf("fib(%d) = %d\n", f, fib_value)

    return fib_value
}
