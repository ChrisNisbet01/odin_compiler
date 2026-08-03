package test_build_ignore_import_helper

#+build ignore

// Build-ignored helper used by the clobber expected-to-fail test.
// Importing this must not disable analysis of the importing file.

helper_val :: proc() -> int {
    return 42
}
