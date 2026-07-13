package main
import "core:os"

main :: proc() {
    // Test struct-level alignment
    s: struct #align 16 { x: int; y: int }
    s.x = 10
    s.y = 20
    
    // Field access still works with alignment
    result := s.x + s.y
    
    // Test field-level alignment
    t: struct { a: int #align 8; b: int }
    t.a = 5
    t.b = 7
    result += t.a + t.b
    
    os.exit(result - 42)
}
