# Stage 14: Polymorphic Struct Types — Implementation Plan

## Goal

Support `Box :: struct($T: typeid) { val: T }` — polymorphic struct types
with per-instantiation field types. The struct template is stored as an AST;
when used as a type (e.g., `b: Box(int)`), a concrete specialization is
instantiated by binding the poly parameters.

## Target Syntax

```odin
// Declaration
Box :: struct($T: typeid) {
    val: T
}

Vec2 :: struct($T: typeid) {
    x, y: T
}

IntBox :: struct($T: typeid, $N: int) {
    data: [$N]T
}

// Usage (explicit type parameters required in Phase 14A)
b: Box(int)
b.val = 42

v: Vec2(f64)
v.x = 1.0
v.y = 2.0

// Multi-param
ib: IntBox(float, 3)
ib.data[0] = 1.0

// Pointer to poly struct type
p: ^Box(int)
```

## Implementation Phases

### Phase 14A (DONE): Core poly struct support

1. Grammar: `struct($T: typeid)` syntax + `Box(int)` type application
2. Semantic: Detect poly structs, instantiate types at usage sites
3. IR: Use concrete type for allocation and field access
4. NO struct literal construction, NO type inference

### Phase 14A.5 (DONE): `$N` int params in poly structs

- Grammar: Extended `TypeApplication` to accept `IntegerLiteral` arguments
  alongside `TypePrefix`, enabling `IntBox(int, 3)` syntax.
- Semantic: `sem_resolve_type_application` detects whether each param is a
  type param (`$T: typeid`) or int param (`$N: int`/`$N: u32`/etc) by
  checking the param's declared type text. Integer params evaluated via
  `sem_evaluate_constant_int` and bound with `POLY_SLOT_INT`. The existing
  `sem_resolve_array_type` looks up `$N` via `poly_env_lookup_int`, so
  `[$N]T` field types resolve correctly.
- Tests: `test_poly_struct_int_param.odin` (4 subtests).

### Future phases:
- Phase 14B: Struct literal construction `Box(int){val = 42}`
- Phase 14C: Type inference `b := Box{val = 42}`
- Phase 14D: Polymorphic enum/union types

---

## Detailed Design

### 1. Grammar Changes (`odin_grammar.gdl`)

#### 1a. StructType: Add optional ParameterList

```
StructType = KwStruct (SoaType | (ParameterList? (Directive IntegerLiteral?)? StructRawBody)) @AST_ACTION_STRUCT_TYPE;
```

This allows:
- `struct { x: int }` — regular struct (no params)
- `struct($T: typeid) { val: T }` — poly struct
- `struct($T: typeid, $N: int) { data: [$N]T }` — multi-param poly struct
- `struct #soa { x: []int }` — SOA struct (unchanged)
- `struct #align 16 { ... }` — aligned struct (unchanged)

The `ParameterList` uses the existing rule that already handles `$T: typeid`.

#### 1b. TypeApplication: New TypePrefix rule

```
TypeApplication = Identifier LParen (TypePrefix (Comma TypePrefix)*)? RParen @AST_ACTION_TYPE_APPLICATION;
```

Added to `TypePrefix` before `Identifier`:
```
TypePrefix = TupleType | PointerType | ... | TypeApplication | Identifier | PolyIdent | MaybeType | SoaType;
```

PEG ordered choice ensures `Box(int)` matches `TypeApplication` before plain
`Identifier` tries to match just `Box`. If there's no `LParen` after the
identifier, `TypeApplication` fails and backtracks to plain `Identifier`.

#### 1c. AllReservedWords update

Add to `AllReservedWords` if needed (probably not — `struct` is already there).

### 2. AST Changes (`odin_grammar_ast.h`)

Add new node type:
```c
AST_NODE_TYPE_APPLICATION,  // Box(int) — poly type instantiation
```

Insert before `AST_NODE_COUNT`. Update `AST_NODE_TYPE_COUNT` in `ast_utils.c`.

### 3. AST Action (`odin_grammar_ast_actions.c`)

```c
DEFINE_ACTION(ast_action_type_application_action, AST_NODE_TYPE_APPLICATION, false)
```

Register: `REGISTER(AST_ACTION_TYPE_APPLICATION, ast_action_type_application_action);`

The action captures children: `[Identifier("Box"), BasicType("int")]`.

### 4. AST Utilities (`ast_utils.c`)

- Update `AST_NODE_TYPE_COUNT` to `AST_NODE_TYPE_APPLICATION + 1`
- Add `[AST_NODE_TYPE_APPLICATION] = false` to `is_type_node_table` — it's
  NOT a raw type node; it resolves to a type through semantic analysis.

### 5. Polymorphism Module (`polymorphism.c/h`)

#### 5a. Poly struct detection

Add `poly_signature_has_type_params()` — walks a StructType AST looking for
`AST_NODE_POLY_IDENT` in its ParameterList children. Similar to
`poly_signature_is_polymorphic` but for struct types.

Actually, simpler: just check if the StructType node has a ParameterList child
containing PolyIdent nodes.

#### 5b. Poly struct origin tracking

Reuse existing `poly_register_origin` / `poly_get_origin`. The origin
ConstantDecl for a poly struct contains the StructType with its ParameterList
and field definitions.

No changes needed — the existing side table already works for any poly symbol.

### 6. Semantic Analyzer (`semantic_analyser.c`)

#### 6a. Pass1: Detect poly structs (in `AST_NODE_CONSTANT_DECL` handler)

After the existing poly proc detection (line 868-878), add:
```c
if (value_node != NULL && value_node->type == AST_NODE_STRUCT_TYPE
    && poly_struct_has_type_params(value_node))
{
    symbol_t * sym = scope_find_symbol_entry(...);
    if (sym) {
        sym->is_polymorphic = true;
        poly_register_origin(sym, node);
    }
}
```

The `poly_struct_has_type_params` function checks if the StructType's
ParameterList contains any `AST_NODE_POLY_IDENT`.

#### 6b. Pass2: Skip poly struct constant declarations

In the `AST_NODE_CONSTANT_DECL` handler (pass2), after the existing
`ProcedureDefinition` branch:
```c
else if (value_node->type == AST_NODE_STRUCT_TYPE)
{
    symbol_t * sym = scope_find_symbol_entry(...);
    if (sym && sym->is_polymorphic) {
        // Poly struct template — skip resolution. Type will be
        // instantiated at usage sites via TypeApplication.
        break;
    }
    // Non-poly struct: resolve normally
    TypeDescriptor const * td = sem_resolve_type_expr(ctx, value_node);
    if (td) { ... }
}
```

#### 6c. TypeApplication handler

In `sem_resolve_type_expr` dispatch table, add:
```c
[AST_NODE_TYPE_APPLICATION] = sem_resolve_type_application,
```

Handler logic:
```c
static TypeDescriptor const *
sem_resolve_type_application(SemContext * ctx, odin_grammar_node_t * node)
{
    // Children: [Identifier("Box"), TypeArg1, TypeArg2, ...]
    odin_grammar_node_t * name_node = node->list.children[0];
    if (!name_node || name_node->type != AST_NODE_IDENTIFIER)
        return NULL;

    symbol_t * sym = scope_find_symbol(..., name_node->text);
    if (!sym || !sym->is_polymorphic) {
        // Not a poly type — error or fall through
        return NULL;
    }

    // Get the origin ConstantDecl → StructType
    odin_grammar_node_t * origin = poly_get_origin(sym);
    odin_grammar_node_t * struct_type = /* extract StructType from origin */;
    if (!struct_type) return NULL;

    // Extract parameter list from struct type
    odin_grammar_node_t * param_list = node_find_child(struct_type, AST_NODE_PARAMETER_LIST);
    if (!param_list) return NULL;

    // Count type args from the TypeApplication node (excluding name_node)
    int arg_count = (int)node->list.count - 1;

    // Build poly env by matching params against type args
    PolyEnv env = {0};
    // ... match each param to corresponding type arg ...
    poly_env_push(ctx, &env);

    // Resolve the struct type with the env active
    TypeDescriptor const * result = sem_resolve_struct_type(ctx, struct_type);

    poly_env_pop(ctx);

    // Cache the result for future lookups with same args
    return result;
}
```

**Important**: `sem_resolve_struct_type` calls `sem_resolve_type_expr` on
each field's type node. When a field type is `$T`, it dispatches to
`sem_resolve_poly_ident_type` which calls `poly_env_lookup_type`. The env
must be active during this call.

**Caching**: Use a simple linear-scan cache keyed on
`(poly_symbol*, type_arg_ptrs[])` to avoid re-resolving the same
instantiation.

### 7. IR Generator (`llvm_ir_generator.c`)

#### 7a. Skip poly struct types in top-level codegen

The existing check `if (sym && sym->is_polymorphic) return NULL;` in
`ir_gen_top_level_decl` already handles this — poly struct types are skipped
(same as poly procs).

#### 7b. TypeApplication in codegen

No special handling needed. The `TypeApplication` node has `resolved_type`
set by the semantic analyzer. The IR generator uses `resolved_type` directly.

When a variable is declared with type `Box(int)`, the variable's type is
a concrete `TD_KIND_STRUCT` with proper LLVM type. All existing struct
allocation, field access, etc. code works unchanged.

#### 7c. Variable declaration with poly struct type

The `ir_gen_variable_decl` handler resolves the variable's type through
`var_type`. If `var_type` is a concrete struct (from TypeApplication), the
LLVM alloca and zero-initialization work normally.

### 8. Tests

#### 8a. `tests/test_poly_struct.odin`
```odin
package test
import "core:os"
import "core:fmt"

Box :: struct($T: typeid) {
    val: T
}

Vec2 :: struct($T: typeid) {
    x, y: T
}

main :: proc() {
    // Basic: poly struct with int
    b: Box(int)
    b.val = 42
    if b.val != 42 do os.exit(1)

    // Basic: poly struct with f64
    b2: Box(f64)
    b2.val = 3.14
    if b2.val != 3.14 do os.exit(2)

    // Multi-field: Vec2 with int
    v: Vec2(int)
    v.x = 10
    v.y = 20
    if v.x + v.y != 30 do os.exit(3)

    // Multi-field: Vec2 with f64
    v2: Vec2(f64)
    v2.x = 1.5
    v2.y = 2.5
    if v2.x + v2.y != 4.0 do os.exit(4)

    // Separate variables of same poly type
    a1: Box(int)
    a1.val = 100
    a2: Box(int)
    a2.val = 200
    if a1.val != 100 || a2.val != 200 do os.exit(5)

    // Different instantiations are different types
    bi: Box(int)
    bf: Box(f64)
    bi.val = 1
    bf.val = 2.0
    if bi.val != 1 do os.exit(6)

    fmt.printf("all poly struct tests passed\n")
    os.exit(0)
}
```

#### 8b. `tests/test_poly_struct_ptr.odin`
```odin
package test
import "core:os"

Box :: struct($T: typeid) {
    val: T
}

set_val :: proc(p: ^Box(int), v: int) {
    p.val = v
}

get_val :: proc(p: ^Box(int)) -> int {
    return p.val
}

main :: proc() {
    b: Box(int)
    b.val = 10
    set_val(&b, 42)
    if get_val(&b) != 42 do os.exit(1)

    fmt.printf("poly struct pointer tests passed\n")
    os.exit(0)
}
```

#### 8c. `tests/test_poly_struct_multi_param.odin`
```odin
package test
import "core:os"

IntBox :: struct($T: typeid, $N: int) {
    data: [$N]T
}

main :: proc() {
    ib: IntBox(int, 3)
    ib.data[0] = 10
    ib.data[1] = 20
    ib.data[2] = 30
    if ib.data[0] + ib.data[1] + ib.data[2] != 60 do os.exit(1)

    fb: IntBox(f64, 2)
    fb.data[0] = 1.0
    fb.data[1] = 2.0
    if fb.data[0] + fb.data[1] != 3.0 do os.exit(2)

    fmt.printf("multi-param poly struct tests passed\n")
    os.exit(0)
}
```

#### 8d. `tests/expected_to_fail/test_poly_struct_type_mismatch.odin`
```odin
package test

Box :: struct($T: typeid) {
    val: T
}

main :: proc() {
    b: Box(int) = Box(f64) // type mismatch
}
```

---

## Key Design Decisions

### Why require explicit type parameters (no inference)?

Type inference for poly structs requires:
1. The initializer expression to carry type information
2. The compiler to propagate types backward from initializer to type params
3. Special handling for struct literals, function returns, etc.

This is a significant additional complexity. Phase 14A requires explicit
`Box(int)` syntax, which is unambiguous and straightforward.

### Why reuse the poly env stack?

The poly env stack is already thread-global in `SemContext` and works with
`sem_resolve_type_expr` dispatch. Since `sem_resolve_struct_type` calls
`sem_resolve_type_expr` for each field type, pushing the env before
resolution automatically resolves `$T` → concrete type in field types.

### Why cache instantiations?

Two variables `b1: Box(int)` and `b2: Box(int)` should share the same
TypeDescriptor (same struct layout). Without caching, each would create
a new TypeDescriptor + LLVM struct type, wasting memory and potentially
breaking GEP indices if the layouts differ.

The cache key is `(poly_symbol*, [type_arg_ptrs...])`. On cache hit, the
existing TypeDescriptor is returned.

### Struct literal construction (deferred)

`Box(int){val = 42}` requires:
1. Grammar: compound literal support in expressions
2. Semantic: validate fields against expected type
3. IR: allocate, set fields, return pointer

This is orthogonal to poly struct support and can be added in Phase 14B.

## Files to Modify

1. `src/odin_grammar.gdl` — StructType params + TypeApplication rule
2. `src/odin_grammar_ast.h` — AST_NODE_TYPE_APPLICATION enum
3. `src/odin_grammar_ast_actions.c` — AST_ACTION_TYPE_APPLICATION
4. `src/ast_utils.c` — update AST_NODE_TYPE_COUNT
5. `src/polymorphism.h` — add poly_struct_has_type_params declaration
6. `src/polymorphism.c` — implement poly_struct_has_type_params
7. `src/semantic_analyser.c` — pass1 detection, pass2 skip, TypeApplication handler
8. `src/sem_type_resolver.c` — TypeApplication dispatch table entry
9. `src/llvm_ir_generator.c` — (no changes expected — existing skip handles it)
10. `tests/test_poly_struct.odin` — core tests
11. `notes/poly_struct_plan.md` — this document

## Risks

1. **Shared AST across instantiations**: `sem_resolve_struct_type` writes to
   `node->resolved_type`. Multiple specializations share the same AST. This
   means the last specialization "wins" for the node annotation, but we use
   the returned TypeDescriptor, not the annotation. Should be fine.

2. **`poly_env_pop` freeing strdup'd names**: The env push copies the struct
   (shallow), so pop frees the copies' names. The local `env` variable still
   has pointers to the freed names. Solution: extract all needed info before
   pushing (same pattern as poly proc instantiation).

3. **Type resolution ordering**: If a poly struct field references another
   poly type that hasn't been resolved yet, we could get circular resolution.
   Solution: detect and error on recursive poly struct types (out of scope).
