package main

#+build windows

// "windows" is not the "ignore" tag, so this file must NOT be skipped.
// The undefined identifier below is a hard semantic error; if this file
// compiled successfully, we would be over-ignoring non-ignore tags.

main :: proc() {
    undefined_symbol_here
}
