package main
import "core:fmt"
import "core:os"

// Direct call to variadic
inner_int :: proc(args: ..any) -> int {
    v := args[0]
    return v.(int)
}

inner_len :: proc(args: ..any) -> int {
    return len(args)
}

// Wrapper of variadic -> variadic (the bug: ..any forwarded as []any)
outer_int :: proc(args: ..any) -> int {
    return inner_int(args)
}

outer_len :: proc(args: ..any) -> int {
    return inner_len(args)
}

// Multi-hop: outer -> middle -> inner
middle_int :: proc(args: ..any) -> int {
    return inner_int(args)
}

multi_hop :: proc(args: ..any) -> int {
    return middle_int(args)
}

main :: proc() {
    // Sanity: direct calls
    direct_int := inner_int(42)
    if direct_int != 42 do os.exit(1)
    fmt.println("PASS: direct inner_int(42)")

    direct_len := inner_len(1, 2, 3)
    if direct_len != 3 do os.exit(2)
    fmt.println("PASS: direct inner_len(1,2,3)")

    // Single forward of int value
    fwd_int := outer_int(42)
    if fwd_int != 42 do os.exit(3)
    fmt.println("PASS: forward outer_int(42) -> inner_int(42)")

    // Single forward of length
    fwd_len := outer_len(1, 2, 3)
    if fwd_len != 3 do os.exit(4)
    fmt.println("PASS: forward outer_len(1,2,3) -> inner_len(3)")

    // Multi-hop forwarding (outer -> middle -> inner)
    multi := multi_hop(42)
    if multi != 42 do os.exit(5)
    fmt.println("PASS: multi_hop outer -> middle -> inner")

    fmt.println("ALL variadic forwarding tests passed")
}
