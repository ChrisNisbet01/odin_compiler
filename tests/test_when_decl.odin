package main

when true {
    X :: 100
}

when false {
    Y :: 200
}

when false {
    Z :: 300
} else {
    W :: 400
}

main :: proc() -> int {
    result := X + W
    return result - 500
}
