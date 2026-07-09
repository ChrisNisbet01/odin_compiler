package main
import "core:os"

main :: proc() {
    // Test int_to_string with various values
    s1: string = int_to_string(0)
    print_string(s1)
    print_string(", ")

    s2: string = int_to_string(42)
    print_string(s2)
    print_string(", ")

    s3: string = int_to_string(-7)
    print_string(s3)
    print_string(", ")

    s4: string = int_to_string(9223372036854775807)
    print_string(s4)
    print_string("\n")

    os.exit(0)
}
