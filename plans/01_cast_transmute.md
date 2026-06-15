# Plan: `cast(T)expr` and `transmute(T)expr`

## Summary
Add Odin's `cast(T)expr` and `transmute(T)expr` expressions. `cast` is a
checked type conversion; `transmute` is an unchecked bit reinterpretation.

## Grammar Changes (`odin_grammar.gdl`)
Add `CastTransmuteExpr` rule between `PrimaryExpression` and the existing
postfix chain:

```
CastExpr = (KwCast | KwTransmute) LParen TypeName RParen UnaryExpression
           @AST_ACTION_CAST_EXPR;
```

Insert it in the `UnaryExpression` alternatives (before postfix).

New AST node: `AST_NODE_CAST_EXPR`
- children[0] = type (`AST_NODE_TYPE_NAME` or type node)
- children[1] = expression (the value to cast/transmute)
- `text` = "cast" or "transmute" (for differentiation)

## AST Actions (`odin_grammar_ast_actions.c/h`)
- Add `handle_cast_expr()` action
- Creates `AST_NODE_CAST_EXPR` with 2 children: type node + expression
- Sets `node->text` to "cast" or "transmute" based on keyword

## Semantic Analysis (`semantic_analyser.c`)
In `sem_evaluate_expr`:
- `AST_NODE_CAST_EXPR`: resolve target type, evaluate expr, validate castability
  - For `transmute`: assert same byte size between source and dest type
  - For `cast`: allow numeric conversions, pointer casts (store error for now if type mismatch)
  - Set `node->resolved_type` = target type

## IR Generation (`llvm_ir_generator.c`)
In `ir_gen_node`:
- `AST_NODE_CAST_EXPR`:
  - Generate expression value
  - Get target LLVM type from `node->resolved_type`
  - If `cast`: use `LLVMBuildIntCast2`, `LLVMBuildFPCast`, or `LLVMBuildPointerCast`
    depending on source/dest types
  - If `transmute`: use `LLVMBuildBitCast` (types must be same size)

## Tests
- `test_cast.odin`: cast between int types, int-to-float, float-to-int, pointer cast
- `test_transmute.odin`: transmute between same-size types, u64<->f64

## Files Changed
- `src/odin_grammar.gdl`
- `src/odin_grammar_ast.h`
- `src/odin_grammar_ast_actions.c`
- `src/odin_grammar_ast_actions.h`
- `src/semantic_analyser.c`
- `src/llvm_ir_generator.c` (maybe `src/llvm_ir_generator.h`)
