package main
import "core:os"

get_context_index :: proc() -> int {
    return context.user_index
}

main :: proc() {
    context.user_index = 42
    val := get_context_index()
    os.exit(val - 42)
}
