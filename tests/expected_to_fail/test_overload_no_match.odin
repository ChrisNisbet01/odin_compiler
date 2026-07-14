package test_overload_no_match

foo_int :: proc(x: int) -> int { return x }
foo_str :: proc(x: string) -> int { return len(x) }
foo :: proc{foo_int, foo_str}

main :: proc() {
    // No candidate matches f64 — should trigger "no matching overload" error
    r := foo(3.14)
}
