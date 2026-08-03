package main

import "test_build_ignore_import_helper"

main :: proc() {
    // The imported helper package is build-ignored, so its symbols are
    // unavailable; the main file must still compile and run normally.
}
