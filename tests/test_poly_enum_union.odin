package test_poly_enum_union

import "core:os"

// Test that polymorphic enum/union syntax is accepted
// Note: Full poly enum/union support requires additional work

// Polymorphic enum - syntax test
Color :: enum($T: typeid)
{
    Red,
    Green,
    Blue,
}

main :: proc() {
    // Create poly enum types
    c: Color(int)
    
    // Basic type operations should work
    if c != c { os.exit(1) }
    
    os.exit(0)
}