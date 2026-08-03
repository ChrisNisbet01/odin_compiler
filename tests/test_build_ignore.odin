package main

#build[ignore]

// This entire file should be ignored by the compiler

test_var: int = 42  // This should not cause any errors

my_proc :: proc() {
    return test_var * 2
}

// Build ignore test - if this compiles without errors, it works
// Note: main is still generated but the file's content is skipped