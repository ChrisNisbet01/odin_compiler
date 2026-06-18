package main

main :: proc() -> int {
    result := 0
    when true {
        result = 99
    }
    when false {
        result = result + 1
    }
    return result - 99
}
