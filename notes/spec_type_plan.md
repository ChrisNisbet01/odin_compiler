# `$M/type` Polymorphic Specialization Syntax

## Goal
Allow polymorphic procs to capture the full concrete type into a named poly identifier while constraining structure and binding nested polys:
```odin
determinant :: proc(m: $M/matrix[1,1]$T) -> T { ... }
// $M = matrix[2,2]f64, $T = f64 when called with matrix[2,2]f64
```

## Steps (in order)

### 1. Grammar (`odin_grammar.gdl`)
- Add `SpecOperator = lexeme('/' not('=')) @AST_ACTION_SPEC_OPERATOR;`
- Add `SpecType = PolyIdent SpecOperator TypePrefix @AST_ACTION_SPEC_TYPE;`
- Insert `SpecType` before `PolyIdent` in `TypePrefix` alternatives (PEG ordered choice)

### 2. AST enums (`odin_grammar_ast.h`)
- Add `AST_NODE_SPEC_OPERATOR` and `AST_NODE_SPEC_TYPE` before `AST_NODE_COUNT`

### 3. Actions (`odin_grammar_ast_actions.c`)
- `DEFINE_TERMINAL_ACTION` for SpecOperator
- `DEFINE_ACTION(ast_action_spec_type)` — 3 children: PolyIdent, SpecOperator, TypePrefix
- `REGISTER` calls in the registration table

### 4. Node names (`ast_node_name.c`)
- Add case entries for both new node types

### 5. Type-node table (`ast_utils.c`)
- Add `[AST_NODE_SPEC_TYPE] = true` so it's recognized as a type node

### 6. Type resolver (`sem_type_resolver.c`)
- Add `sem_resolve_spec_type` forward decl + dispatch entry
- Implementation: at instantiation time, looks up `$M` in poly env, returns bound type

### 7. Polymorphism (`polymorphism.c`)
- Add `AST_NODE_SPEC_TYPE` case in `poly_unify_poly_idents_in_type`:
  - Binds `$M` to full arg type
  - Recurses into pattern (child 2) for nested poly binding + constraint checking

### 8. Disable param hoisting check for SpecType params
- If Mechanism A (`poly_build_env_from_args` skips SpecType), ensure it isn't accidentally caught by the "param not recognised" assert

## Build & Test
- `make -j$(nproc)` after each step group
- `bash tests/run_tests.sh` to verify all 213+ tests pass
- Check `test_matrix_basic.odin` with `matrix[1,1]f64` via determinant
