package main

#+build   ignore

// Extra whitespace between "#+build" and the tag must still be recognized.
// This entire file is ignored; a dummy main is generated.

test_var: int = 99

my_proc :: proc() -> int {
    return test_var
}
