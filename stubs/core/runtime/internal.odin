package runtime

@(builtin)
mem_free_all :: proc(allocator := context.allocator, loc := #caller_location) -> (err: Allocator_Error) {
    if allocator.procedure != nil {
        _, err = allocator.procedure(allocator.data, .Free_All, 0, 0, nil, 0, loc)
    }
    return
}

@(builtin)
free_all :: proc{mem_free_all}