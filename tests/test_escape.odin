package main

import "core:fmt"
import "core:os"

main :: proc() {
    // Test basic escape sequences
    s1 := "\n"
    if len(s1) != 1 { os.exit(1) }
    if s1[0] != '\n' { os.exit(2) }

    s2 := "\t"
    if len(s2) != 1 { os.exit(3) }
    if s2[0] != '\t' { os.exit(4) }

    s3 := "\\"
    if len(s3) != 1 { os.exit(5) }
    if s3[0] != '\\' { os.exit(6) }

    s4 := "\""
    if len(s4) != 1 { os.exit(7) }
    if s4[0] != '"' { os.exit(8) }

    s5 := "\r"
    if len(s5) != 1 { os.exit(9) }
    if s5[0] != '\r' { os.exit(10) }

    // Test less common escapes
    s6 := "\0"
    if len(s6) != 1 { os.exit(11) }
    if s6[0] != 0 { os.exit(12) }

    s7 := "\'"
    if len(s7) != 1 { os.exit(13) }
    if s7[0] != '\'' { os.exit(14) }

    s8 := "\a"
    if len(s8) != 1 { os.exit(15) }
    if s8[0] != '\a' { os.exit(16) }

    s9 := "\b"
    if len(s9) != 1 { os.exit(17) }
    if s9[0] != '\b' { os.exit(18) }

    s10 := "\f"
    if len(s10) != 1 { os.exit(19) }
    if s10[0] != '\f' { os.exit(20) }

    s11 := "\v"
    if len(s11) != 1 { os.exit(21) }
    if s11[0] != '\v' { os.exit(22) }

    s12 := "\e"
    if len(s12) != 1 { os.exit(23) }
    if s12[0] != '\e' { os.exit(24) }

    // Test hex escape
    s13 := "\x20"
    if len(s13) != 1 { os.exit(25) }
    if s13[0] != ' ' { os.exit(26) }

    s14 := "\x41"
    if len(s14) != 1 { os.exit(27) }
    if s14[0] != 'A' { os.exit(28) }

    s15 := "\x1b"
    if len(s15) != 1 { os.exit(29) }
    if s15[0] != '\e' { os.exit(30) }

    // Test mixed escapes in one string
    s16 := "A\nB\tC"
    if len(s16) != 5 { os.exit(31) }
    if s16[0] != 'A' { os.exit(32) }
    if s16[1] != '\n' { os.exit(33) }
    if s16[2] != 'B' { os.exit(34) }
    if s16[3] != '\t' { os.exit(35) }
    if s16[4] != 'C' { os.exit(36) }

    // Test hex escapes mixed with normal chars
    s17 := "\x48\x65\x6c\x6c\x6f"
    if len(s17) != 5 { os.exit(37) }
    if s17[0] != 'H' { os.exit(38) }
    if s17[1] != 'e' { os.exit(39) }
    if s17[2] != 'l' { os.exit(40) }
    if s17[3] != 'l' { os.exit(41) }
    if s17[4] != 'o' { os.exit(42) }

    // Test that raw strings don't process escapes
    s18 := `\n\t`
    if len(s18) != 4 { os.exit(43) }
    if s18[0] != '\\' { os.exit(44) }
    if s18[1] != 'n' { os.exit(45) }
    if s18[2] != '\\' { os.exit(46) }
    if s18[3] != 't' { os.exit(47) }

    // Test fmt.println with escape sequences (output via println)
    fmt.println("escape:", "\x41\x42\x43")

    os.exit(0)
}
