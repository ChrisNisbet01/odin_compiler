package test_build_ignore_import_helper

#+build ignore

// This entire helper package is build-ignored. Its symbols must never be
// registered, and importing it must not affect compilation of the main file.

helper_val :: proc() -> int {
    return 42
}
