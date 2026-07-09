package fibonacci

import "core:fmt"
import "core:os"

fib :: proc(v: u64) -> u64 {
    if v == 0 || v == 1 {
        return 1
    }
    return fib(v - 1) + fib(v - 2)
}

main :: proc() {
    fmt.println("Hello, World!")

    fib_val := run()

    os.exit(fib_val)
}

run :: proc() -> u64 {
    f := 6
    fib_value := fib(f)

    fmt.printf("fib(%d) = %d\n", f, fib_value)

    return fib_value
}
