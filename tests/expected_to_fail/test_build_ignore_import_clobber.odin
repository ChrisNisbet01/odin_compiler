package main

import "test_build_ignore_import_helper"

// Importing a build-ignored package must NOT mark the main file as
// build-ignored. The undefined identifier below is a hard semantic error;
// if this file compiled successfully, the ignored import would have
// clobbered the main file's build_ignored flag (causing a silent dummy main).

main :: proc() {
    undefined_symbol_here
}
