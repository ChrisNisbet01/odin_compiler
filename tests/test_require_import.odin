package main

@require import "test_import_helper"

main :: proc() {
    // We do NOT use anything from test_import_helper.
    // The import should be skipped during codegen.
    os_exit(0)
}