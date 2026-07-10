package main
import "core:os"

main :: proc() {
    // Test int_to_string with various values
    s1: string = int_to_string(0)
    print_string(1, s1)
    print_string(1, ", ")

    s2: string = int_to_string(42)
    print_string(1, s2)
    print_string(1, ", ")

    s3: string = int_to_string(-7)
    print_string(1, s3)
    print_string(1, ", ")

    s4: string = int_to_string(9223372036854775807)
    print_string(1, s4)
    print_string(1, "\n")

    os.exit(0)
}
