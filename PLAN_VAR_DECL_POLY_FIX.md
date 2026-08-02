# Fix for `:=` Poly Call Result Type Inference Bug

## Problem
When using `:=` short variable declaration with poly calls like:
```odin
d := linalg.determinant(m)
```
The result type was `kind=0` (NULL/dereferenced) instead of the correct matrix element type.

## Root Cause
In `semantic_analyser.c` `sem_analyse_variable_decl` (lines 2281-2304), the `poly_expected_return_type` context field is only set when there's an explicit declared type:
```c
if (type_node != NULL && var_type != NULL)
    ctx->poly_expected_return_type = var_type;
```

But for `:=` declarations, `type_node == NULL`, so poly calls can't infer the return type from the expected type.

## Secondary Bug Found and Fixed
In `sem_evaluate_expr.c` `sem_evaluate_postfix_expr`, the package-qualified poly call handling had a bug at line 1988:
```c
if (i > 0)
{
    odin_grammar_node_t * prev_op = postfix_ops->list.children[i-1];
    if (prev_op && prev_op->resolved_symbol)
        pkg_callee_sym = prev_op->resolved_symbol;
}

if (pkg_callee_sym && pkg_callee_sym->is_polymorphic)
{
    // ... poly resolution
}
```

When `i == 0` (first postfix op for direct calls like `linalg.determinant(n)`), the poly check was skipped entirely, causing poly calls to fall through to the unspecialized proc return type (which is NULL for `$T` return positions).

## Fix Applied
Added an `else` branch to get the callee symbol from the base expression when `i == 0` in `src/sem_evaluate_expr.c`:

```c
if (i > 0)
{
    odin_grammar_node_t * prev_op = postfix_ops->list.children[i-1];
    if (prev_op && prev_op->resolved_symbol)
        pkg_callee_sym = prev_op->resolved_symbol;
}
else
{
    // First postfix op (i == 0): get callee from base expression
    odin_grammar_node_t * base = node->list.children[0];
    if (base != NULL)
    {
        odin_grammar_node_t * inner = base;
        while (inner->type == AST_NODE_PRIMARY_EXPRESSION && inner->list.count > 0)
            inner = inner->list.children[0];
        if (inner->type == AST_NODE_POSTFIX_EXPRESSION && inner->list.count >= 2)
        {
            odin_grammar_node_t * inner_ops = inner->list.children[1];
            if (inner_ops && inner_ops->list.count > 0)
            {
                odin_grammar_node_t * last_member = inner_ops->list.children[inner_ops->list.count - 1];
                if (last_member && last_member->resolved_symbol)
                    pkg_callee_sym = last_member->resolved_symbol;
            }
        }
    }
}

if (pkg_callee_sym && pkg_callee_sym->is_polymorphic)
{
    // ... poly resolution (now works for i == 0)
}
```

## Status: FIXED ✅
All 235 tests pass including:
- `test_polymorphic_basics.odin` (uses `:=` with poly calls)
- `test_matrix_basic.odin` (uses `linalg.determinant` with `:=`)
- `test_matrix_vector.odin`