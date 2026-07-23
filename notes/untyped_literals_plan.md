# Untyped Literals — Architecture Design

## Problem Statement

Currently, numeric literals receive a **fixed default type** during semantic
analysis:

- Integer literals (`0`, `42`, `-1`) → `int` (i64)
- Float literals (`3.14`, `2.0`) → `f64`

This is wrong. In Odin (and most statically typed languages), untyped
numeric literals are **type-ambiguous** — they adapt to whatever type
the context demands. `0` is neither `int` nor `f64` — it's an untyped
integer that can become either.

### What breaks without untyped literals

| Construct | Current behavior | Correct behavior |
|---|---|---|
| `foo(0)` where `foo :: proc{foo_u8, foo_f64}` | Overload resolution sees `int`, picks `foo_u8` even if `foo_f64` is "better" | Both candidates match; disambiguate by specificity |
| `identity(0)` binding `$T` | `$T = int`, always | `$T` should bind to whatever the context requires |
| `typeid_of(0) == typeid_of(int)` | Always true (0 is int) | Should be context-dependent or error |
| `x: f32 = 0` | Works via IR coercion | Works, but the semantic layer thinks `int` → `f32` |
| `switch x { case 0: }` on `u8` | "cannot assign 'int' to 'u8'" | `0` adapts to `u8` |

The Phase 1 fix (allowing `INTEGER_VALUE → is_floating_kind` in
`sem_can_implicitly_convert`) handles return/assignment checks but
doesn't fix overload resolution or polymorphic binding.

## Design Goals

1. **Literals are type-ambiguous** — `0` has type `untyped_int`, not `int`
2. **Context propagation** — when a literal appears where a specific type
   is expected, it adopts that type
3. **Overload resolution** — untyped literals match multiple candidates
   equally, disambiguated by specificity
4. **Polymorphic binding** — `$T` can bind to `untyped_int` when no
   concrete type is expected, and specialize later
5. **Backward compatible** — all existing tests pass without changes
6. **Incremental** — can be implemented in stages

## Architecture

### Stage A: Type Descriptor for Untyped Literals

Add two new type kinds to `TypeDescriptors`:

```
TD_KIND_UNTYPED_INT    — integer literal without a fixed type
TD_KIND_UNTYPED_FLOAT  — float literal without a fixed type
```

These are **singleton types** — one global `untyped_int_type` and one
`untyped_float_type` in the type registry, like `TD_KIND_VOID`.

**No LLVM type** — these are compile-time-only. At IR emission, the
literal is coerced to the context type.

**Files:**
- `type_descriptors.h`: add `TD_KIND_UNTYPED_INT`, `TD_KIND_UNTYPED_FLOAT`
  to the `TypeDescriptorKind` enum; add getter functions
  `get_untyped_int_type(TypeDescriptors * reg)` and
  `get_untyped_float_type(TypeDescriptors * reg)`
- `type_descriptors.c`: implement the getters (lazy-create singleton
  descriptors); register in `register_builtin_context_types`
- `type_descriptors.h`: add `bool is_untyped_kind(TypeDescriptor const *)`

### Stage B: Semantic Analysis Assigns Untyped Type

**`sem_evaluate_integer_value`** (`sem_evaluate_expr.c:149`):
```c
// Before:
node->resolved_type = get_basic_type_by_name(ctx->type_registry, "int");

// After:
node->resolved_type = get_untyped_int_type(ctx->type_registry);
```

**`sem_evaluate_float_value`** (`sem_evaluate_expr.c:162`):
```c
// Before:
node->resolved_type = flt_type;  // f64

// After:
node->resolved_type = get_untyped_float_type(ctx->type_registry);
```

**Immediate effect:** All integer literals become `untyped_int` and all
float literals become `untyped_float`. This will break ~50 tests
temporarily — that's expected and fixed in subsequent stages.

### Stage C: Context-Driven Type Coercion

When an expression with `untyped_int` or `untyped_float` type appears
in a context that expects a concrete type, **rewrite the resolved_type**
on the literal node to the concrete type.

This is the core of the feature. Contexts that provide type information:

#### C1: Variable declarations (`ir_gen_var_decl.c` / `semantic_analyser.c`)

```
x: u8 = 0
```

After evaluating the init expression (`0` → `untyped_int`), check if
`var_type` is concrete. If so, and the init is untyped, set
`init_node->resolved_type = var_type`.

**Location:** `semantic_analyser.c:1691` (var decl handler, after
`sem_evaluate_expr` returns)

#### C2: Return statements (`semantic_analyser.c`)

```
return 0
```

After evaluating the return expression, check against
`expected_return_type`. If the expr is untyped and the expected type
is concrete, rewrite.

**Location:** `semantic_analyser.c:338` (return type check)

#### C3: Function arguments (`semantic_analyser.c` / `sem_evaluate_expr.c`)

```
foo(0)  where foo :: proc(x: u8)
```

After evaluating each argument, check against the candidate parameter
type. If untyped, rewrite to the parameter type.

**Location:** `sem_resolve_overload_bundle_call` (argument evaluation
loop) and `sem_evaluate_postfix_expr` (regular call argument loop)

#### C4: Binary expression RHS (`semantic_analyser.c`)

```
x + 0  where x: f64
```

The RHS is checked against the LHS type. If RHS is untyped, rewrite.

**Location:** Binary expression handler (after LHS type is known)

#### C5: Comparison operators

```
if x == 0 where x: u8
```

Both sides of `==`/`!=` should agree on type. If one side is concrete
and the other is untyped, coerce the untyped side.

#### C6: Assignments

```
x = 0  where x: u8
```

Same as C1 — after evaluating the RHS, coerce to the LHS type.

**Implementation pattern for all contexts:**

```c
// In each coercion site:
if (is_untyped_kind(expr_type) && is_concrete_kind(expected_type))
{
    expr_node->resolved_type = expected_type;
    expr_type = expected_type;
}
```

A helper function `sem_coerce_untyped(odin_grammar_node_t * node,
TypeDescriptor const * target)` would centralize this.

### Stage D: `sem_can_implicitly_convert` Cleanup

With untyped types properly resolved, `sem_can_implicitly_convert`
simplifies:

```c
bool sem_can_implicitly_convert(SemContext * ctx,
    odin_grammar_node_t * expr_node,
    TypeDescriptor const * from_type, TypeDescriptor const * to_type)
{
    (void)ctx; (void)expr_node;
    if (from_type == to_type) return true;
    if (from_type == NULL || to_type == NULL) return false;
    // Untyped literals convert to any numeric type
    if (is_untyped_kind(from_type)
        && (is_integer_kind(to_type) || is_floating_kind(to_type)))
        return true;
    // ... existing distinct-type and same-kind rules ...
    return false;
}
```

The Phase 1 hack (checking AST node type) can be removed — the
resolved type now carries the untyped-ness directly.

### Stage E: IR Generation

Literals with untyped types need to produce the right LLVM value.

**`ir_gen_integer_value`** (`llvm_ir_generator.c:239`):
- If `resolved_type` is `untyped_int`, use the **context type** (the
  type expected by the surrounding expression)
- If no context type is available, default to `i64` (current behavior)

**`ir_gen_float_value`** (`llvm_ir_generator.c:339`):
- Same pattern: if `untyped_float`, use context type, else default to
  `double`

**Context type propagation to IR:** Each IR generation call site
already has a target type available:
- `ir_gen_variable_decl`: `var_type->llvm_type`
- `ir_gen_return_statement`: `ret_llvm_type`
- `ir_gen_binary_expression`: `lhs_type->llvm_type`
- `ir_gen_postfix_call`: `pm->params[i]->llvm_type`

For literal nodes, `ir_gen_node` should pass the context type down to
`ir_gen_integer_value` / `ir_gen_float_value`.

### Stage F: Polymorphic Binding

**`poly_build_env_from_args`** (`polymorphism.c:550`):
```c
// Before:
arg_types[arg_count] = chain_args[ci] ? chain_args[ci]->resolved_type : NULL;

// After:
TypeDescriptor const * arg_type = chain_args[ci]
    ? chain_args[ci]->resolved_type : NULL;
// If arg is untyped and param is concrete ($N: int), coerce
if (is_untyped_kind(arg_type) && param_is_concrete)
    arg_type = param_concrete_type;
arg_types[arg_count] = arg_type;
```

For `$T: typeid` params: an untyped arg means `$T` should bind to the
untyped type. This is correct — it allows `$T` to be specialized later
based on usage context.

For `$N: int` params: an untyped integer arg should bind to `int`.

### Stage G: Overload Resolution

**`sem_resolve_overload_bundle_call`** (`sem_evaluate_expr.c:1973`):
- Pass `expr_node` instead of `NULL` to `sem_types_assignable`
- Or: check `is_untyped_kind(arg_type)` before the assignment check
  and consider it assignable to any numeric parameter type

Both approaches work. The second is simpler and doesn't require
threading the AST node through.

## Implementation Order

| Stage | Scope | Risk | Dependencies |
|---|---|---|---|
| A | Type descriptors | Low | None |
| B | Semantic assigns untyped | Low | A (breaks ~50 tests) |
| C1-C6 | Context coercion | Medium | B |
| D | Simplify sem_can_implicitly_convert | Low | C |
| E | IR generation | Medium | C |
| F | Polymorphic binding | Low | C |
| G | Overload resolution | Low | C |

**Recommended batches:**
1. **A+B+C+D** — semantic layer complete; tests pass (literals resolve
   to context type at analysis time)
2. **E** — IR layer complete; codegen works
3. **F+G** — poly/overload complete

## Estimated Scope

- New code: ~200-300 lines
- Modified files: `type_descriptors.c/h`, `sem_check.c`,
  `sem_evaluate_expr.c`, `semantic_analyser.c`, `llvm_ir_generator.c`,
  `polymorphism.c`, `ir_gen_var_decl.c`, `ir_gen_statement.c`
- Tests: update existing literal-related tests, add ~5-10 new tests
- Estimated complexity: medium (conceptually clean, but touches the
  core expression evaluator)

## Risks

1. **Expression wrapper unwrapping** — The compiler wraps expressions in
   `AST_NODE_EXPRESSION` / `AST_NODE_ASSIGN_EXPRESSION` chains. Context
   coercion must unwrap these to find the literal node. Existing helpers
   (`sem_collect_comma_chain_args` pattern) handle this.

2. **Nested contexts** — A literal inside a nested expression
   (`x + 0 * y`) needs to inherit the context from the outermost
   coercion site. The inner `0` should become the type of `x`, not the
   type of `0 * y`. This requires careful ordering: evaluate LHS first,
   then coerce RHS.

3. **Multiple coercion paths** — A literal could be coerced by multiple
   contexts (e.g., `f(0)` where `f :: proc(x: u8) -> u8` — both arg
   and return paths). The first coercion wins; subsequent ones are
   no-ops because `resolved_type` is already concrete.

4. **Compile-time evaluation** — `#assert[0 == 0]` and `when` conditions
   evaluate literals at compile time. The untyped type needs to support
   `==` comparison between two untyped ints. This works naturally
   (untyped == untyped → untyped bool), but the comparison result must
   also be untyped.

## Open Questions

1. Should `untyped_int` and `untyped_float` unify into a single
   `untyped_numeric` type? In Odin they're separate (`0` vs `0.0`).
   Keeping them separate is more correct.

2. How to handle `0.0 + 1`? In Odin, the integer `1` coerces to `f64`.
   This is binary-expression context coercion (Stage C4). The LHS is
   `untyped_float` → becomes `f64` (or whatever the other operand is),
   and the RHS `untyped_int` coerces to the same.

3. Should `typeid_of(0)` return the typeid of the context type or
   `untyped_int`? In real Odin, untyped constants don't have a
   `typeid`. This is a separate feature (typed untyped).
