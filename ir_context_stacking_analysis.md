# IR Generator Context Stacking Analysis

## Date
2026-06-24

## Problem Statement
The current implementation of `ir_gen_top_level_decl()` in `src/llvm_ir_generator.c` uses a flat `ctx->current_function` field without a stack mechanism. This breaks proper IR generation for nested functions.

## Current Implementation Issues

### 1. Context Overwriting
```c
ctx->current_function = func;  // Overwrites outer function context
ctx->current_return_type = proc_type->proc_metadata.return_type;
```

When a nested function is generated, the outer function's context is lost.

### 2. State Pollution
`current_return_type` is similarly overwritten, causing potential type mismatches for nested functions with different return types.

### 3. Incomplete Nested Function Handling
In `ir_gen_nested_procedure_decl`:
```c
ctx->current_function = func;  // Assigns nested function
// No restoration of outer context after nested code
```

## Proposed Solution: Context Stack with Dynamic Array

### 1. New Data Structures
```c
typedef struct {
    LLVMValueRef current_function;
    TypeDescriptor * current_return_type;
} IrGenContextStackEntry;

typedef struct {
    IrGenContextStackEntry * entries;
    size_t count;
    size_t capacity;
} IrGenContextStack;

// Add to IrGenContext:
IrGenContextStack ctx_stack;
```

### 2. Push/Pop Operations
```c
static void ctx_stack_push(IrGenContext * ctx) {
    if (ctx->ctx_stack.count >= ctx->ctx_stack.capacity) {
        ctx->ctx_stack.capacity *= 2;
        ctx->ctx_stack.entries = realloc(ctx->ctx_stack.entries, 
            ctx->ctx_stack.capacity * sizeof(IrGenContextStackEntry));
    }
    ctx->ctx_stack.entries[ctx->ctx_stack.count++] = 
        (IrGenContextStackEntry){ctx->current_function, ctx->current_return_type};
}

static void ctx_stack_pop(IrGenContext * ctx) {
    if (ctx->ctx_stack.count > 0) {
        IrGenContextStackEntry entry = ctx->ctx_stack.entries[--ctx->ctx_stack.count];
        ctx->current_function = entry.current_function;
        ctx->current_return_type = entry.current_return_type;
    }
}
```

### 3. Updated IR Generation Functions
```c
ir_gen_top_level_decl(ctx, node) {
    ctx_stack_push(ctx);  // Save outer context
    ctx->current_function = func;
    ctx->current_return_type = proc_type->return_type;
    // ... existing code ...
    ctx_stack_pop(ctx);   // Restore outer context
}

ir_gen_nested_procedure_decl(ctx, node) {
    ctx_stack_push(ctx);
    ctx->current_function = func;
    // ... generate nested IR ...
    ctx_stack_pop(ctx);
}
```

## Benefits
- Proper context isolation for nested functions
- Dynamic sizing for arbitrary nesting depth
- Backward compatibility (stack operations are transparent)
- Clean restoration of outer context after nested generation

## Files to Modify
- `src/llvm_ir_generator.c` - Add stack struct, push/pop functions, update all context-setting locations