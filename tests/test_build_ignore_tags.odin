package main

#+build windows, ignore, linux

// Comma-separated multi-tag form. "ignore" among the tags means this file
// is skipped even though "windows" and "linux" are not recognized tags.

test_var: int = 7

my_proc :: proc() -> int {
    return test_var
}
