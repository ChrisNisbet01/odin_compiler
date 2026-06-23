package main

get_context_index :: proc() -> int {
    return context.user_index
}

main :: proc() -> int {
    context.user_index = 42
    val := get_context_index()
    return val - 42
}
