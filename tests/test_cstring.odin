package main

main :: proc() -> int {
    // Basic cstring declaration and initialization
    s: cstring = "hello"
    
    // Comparison with nil
    if s == nil {
        return 1
    }
    
    // Multiple cstring vars
    s2: cstring = "world"
    s3: cstring = s2
    
    result: int = 0
    if s != nil { result = result + 1 }
    if s2 != nil { result = result + 1 }
    if s3 != nil { result = result + 1 }
    return result - 3
}
