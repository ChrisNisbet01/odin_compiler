package multi_import_dep

greet :: proc() -> string {
    return "Hello, World"
}

double_len :: proc(s: string) -> int {
    return len(s) * 2
}
