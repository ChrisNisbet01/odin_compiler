package main

import "core:fmt"

main :: proc() -> int {
    // Test basic escape sequences
    s1 := "\n"
    if len(s1) != 1 { return 1 }
    if s1[0] != '\n' { return 2 }

    s2 := "\t"
    if len(s2) != 1 { return 3 }
    if s2[0] != '\t' { return 4 }

    s3 := "\\"
    if len(s3) != 1 { return 5 }
    if s3[0] != '\\' { return 6 }

    s4 := "\""
    if len(s4) != 1 { return 7 }
    if s4[0] != '"' { return 8 }

    s5 := "\r"
    if len(s5) != 1 { return 9 }
    if s5[0] != '\r' { return 10 }

    // Test less common escapes
    s6 := "\0"
    if len(s6) != 1 { return 11 }
    if s6[0] != 0 { return 12 }

    s7 := "\'"
    if len(s7) != 1 { return 13 }
    if s7[0] != '\'' { return 14 }

    s8 := "\a"
    if len(s8) != 1 { return 15 }
    if s8[0] != '\a' { return 16 }

    s9 := "\b"
    if len(s9) != 1 { return 17 }
    if s9[0] != '\b' { return 18 }

    s10 := "\f"
    if len(s10) != 1 { return 19 }
    if s10[0] != '\f' { return 20 }

    s11 := "\v"
    if len(s11) != 1 { return 21 }
    if s11[0] != '\v' { return 22 }

    s12 := "\e"
    if len(s12) != 1 { return 23 }
    if s12[0] != '\e' { return 24 }

    // Test hex escape
    s13 := "\x20"
    if len(s13) != 1 { return 25 }
    if s13[0] != ' ' { return 26 }

    s14 := "\x41"
    if len(s14) != 1 { return 27 }
    if s14[0] != 'A' { return 28 }

    s15 := "\x1b"
    if len(s15) != 1 { return 29 }
    if s15[0] != '\e' { return 30 }

    // Test mixed escapes in one string
    s16 := "A\nB\tC"
    if len(s16) != 5 { return 31 }
    if s16[0] != 'A' { return 32 }
    if s16[1] != '\n' { return 33 }
    if s16[2] != 'B' { return 34 }
    if s16[3] != '\t' { return 35 }
    if s16[4] != 'C' { return 36 }

    // Test hex escapes mixed with normal chars
    s17 := "\x48\x65\x6c\x6c\x6f"
    if len(s17) != 5 { return 37 }
    if s17[0] != 'H' { return 38 }
    if s17[1] != 'e' { return 39 }
    if s17[2] != 'l' { return 40 }
    if s17[3] != 'l' { return 41 }
    if s17[4] != 'o' { return 42 }

    // Test that raw strings don't process escapes
    s18 := `\n\t`
    if len(s18) != 4 { return 43 }
    if s18[0] != '\\' { return 44 }
    if s18[1] != 'n' { return 45 }
    if s18[2] != '\\' { return 46 }
    if s18[3] != 't' { return 47 }

    // Test fmt.println with escape sequences (output via println)
    fmt.println("escape:", "\x41\x42\x43")

    return 0
}
