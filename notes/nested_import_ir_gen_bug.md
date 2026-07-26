# Nested Import IR Generation Bug: Analysis and Fix Plan

## Problem Statement

When package A imports package B, and B imports package C, calling `C.func()` from
within B's body fails during IR generation with a **null operand** error. Semantic
analysis succeeds — the error only manifests when LLVM IR is emitted.

Concrete example:

    // test.odin
    package main
    import "core:fmt"    // <-- fmt now imports strings
    import "core:os"
    main :: proc() {
        fmt.println("hello")
        os.exit(0)
    }

    // stubs/core/fmt/fmt.odin (after adding sb* functions)
    package fmt
    import "core:strings"
    sbprint :: proc(b: ^strings.Builder, s: string) -> int {
        return strings.write_string(b, s)   // <-- NULL OPERAND in IR
    }

Generated IR:

    define i64 @sbprint(ptr %0, ptr %1, { ptr, i64 } %2) {
    entry:
      ...
      %calltmp = call <cannot get addrspace!> i64 <null operand!>(ptr %context, ptr %b1, { ptr, i64 } %s2)
      ret i64 %calltmp
    }

The function `write_string` IS defined later in the module (line 4576), but has no
forward declaration at the call site (line 232).

## Investigation

### 1. Import Registration (semantic_analyser.c:1238-1317)

All imports — direct and transitive — are appended to a **flat array**
`ctx->imports[]`. When B imports C during recursive analysis, C is appended after
B. The array is shared across the entire compilation unit.

    ctx->imports[] = [fmt, strings]   // after main's import of fmt

Each package gets an **isolated scope** (`scope_create(NULL, ...)` — NULL parent),
so package symbols are not in the caller's lexical scope. Package names ("strings",
"fmt") are NOT registered as identifiers in any scope.

### 2. Semantic Analysis (sem_evaluate_expr.c:1604-1822)

Package-qualified expression resolution works by:
1. Unwrapping the base identifier (`strings`)
2. Calling `find_imported_package_by_name(ctx, "strings")` — searches the flat
   `ctx->imports[]` array (**sem_context.c:164-176**)
3. Looking up the member in the package's own scope:
   `scope_find_symbol_entry(pkg->package_scope, "write_string")`
4. Setting `op->resolved_symbol` on the POSTFIX_MEMBER node

This works correctly because all transitive deps are in `ctx->imports[]`.

### 3. Non-Poly vs Poly CALL handling (sem_evaluate_expr.c:1670-1760)

For POSTFIX_CALL in the package-qualified branch:

- **Polymorphic calls** (line 1682-1731): Set `op->resolved_symbol` on the CALL
  node to the specialization symbol.
- **Overload bundle calls** (line 1759-1803): Set `op->resolved_symbol` on the
  CALL node to the resolved winner.
- **Non-polymorphic calls** (line 1734-1757): **Do NOT set `op->resolved_symbol`**
  on the CALL node. Only evaluate args and set `op->resolved_type` to the return
  type.

This asymmetry is the first root cause.

### 4. IR Generation — Import Ordering (llvm_ir_generator.c:3289-3303)

`ir_generate()` processes `ctx->imports[]` in array order:

    for (int i = 0; i < ctx->import_count; i++) {
        ImportedPackage * pkg = ctx->imports[i];
        if (pkg->codegen_done) continue;
        ir_gen_process_ast(ctx, pkg->ast);
        pkg->codegen_done = true;
    }

Since `fmt` is at index 0 and `strings` at index 1, `fmt` is codegen'd FIRST.
When `fmt`'s `sbprint` body calls `strings.write_string`, the strings package
hasn't been codegen'd yet, so `write_string`'s `symbol->value.value` (the
LLVMValueRef) is NULL.

### 5. IR Generation — Postfix Member (ir_gen_postfix.c:604-609)

When processing `strings.write_string`:

    // Package-qualified access: pkg.member (resolved by semantic analyser)
    if (*cur_type == NULL && op->resolved_symbol != NULL)
    {
        *val = op->resolved_symbol->value.value;   // <-- NULL for not-yet-codegen'd
        *cur_type = op->resolved_symbol->value.type_info;
        return;
    }

This blindly copies the potentially-NULL LLVM value without any forward-declaration
fallback.

### 6. IR Generation — Postfix Call (ir_gen_postfix.c:182-241)

Three priority levels for finding the callee:

- **Priority 1** (line 188-206): `op->resolved_symbol` on the CALL node. Has
  forward-declaration logic: if `resolved->value.value` is NULL, calls
  `LLVMGetNamedFunction` / `LLVMAddFunction`. **Does NOT fire** for non-polymorphic
  package-qualified calls (because the semantic analyser didn't set `resolved_symbol`
  on the CALL node).

- **Priority 2** (line 208-229): Scope lookup for the base identifier. Package names
  are not in any scope, so this finds nothing useful.

- **Priority 3** (line 231-236): Falls back to `*cur_type`. Picks up the procedure
  type from the POSTFIX_MEMBER handler. But **does NOT fix `*val` if it's NULL**.

Result: `proc_type` is set, but `val` remains NULL → `LLVMBuildCall2(..., NULL, ...)`
→ null operand in IR.

### Summary of Root Causes

| # | Location | Issue |
|---|----------|-------|
| 1 | `sem_evaluate_expr.c:1734-1757` | Non-poly package-qualified CALL doesn't set `resolved_symbol` on the CALL node |
| 2 | `ir_gen_postfix.c:607` | `ir_gen_postfix_member` copies NULL `value.value` without forward-declaration fallback |
| 3 | `ir_gen_postfix.c:231-236` | `ir_gen_postfix_call` Priority 3 picks up proc type but doesn't create forward declaration for NULL val |
| 4 | `llvm_ir_generator.c:3289-3303` | Import codegen processes in insertion order, no dependency-aware ordering |

## Fix Plan

### Fix A: Set `resolved_symbol` on non-poly CALL nodes (semantic analyser)

**File**: `src/sem_evaluate_expr.c:1734`

In the non-polymorphic POSTFIX_CALL branch, add `op->resolved_symbol = pkg_callee_sym`
before the type check. This makes Priority 1 in `ir_gen_postfix_call` fire for ALL
package-qualified calls, not just polymorphic/overload ones.

    case AST_NODE_POSTFIX_CALL:
    {
        symbol_t * pkg_callee_sym = NULL;
        if (i > 0) {
            ...
            pkg_callee_sym = prev_op->resolved_symbol;
        }

        if (pkg_callee_sym && pkg_callee_sym->is_polymorphic) {
            // ... existing poly path ...
        }
        else if (pkg_callee_sym && pkg_callee_sym->value.type_info)
        {
            op->resolved_symbol = pkg_callee_sym;  // <-- ADD THIS
            TypeDescriptor const * type = pkg_callee_sym->value.type_info;
            if (type && type->kind == TD_KIND_PROC) {
                // ... evaluate args, set op->resolved_type ...
            }
        }
    }

### Fix B: Forward-declare in `ir_gen_postfix_member` (safety net)

**File**: `src/ir_gen_postfix.c:604-609`

When `symbol->value.value` is NULL and the symbol is a procedure, create a forward
declaration. This mirrors the existing logic in `ir_gen_postfix_call` Priority 1.

    if (*cur_type == NULL && op->resolved_symbol != NULL)
    {
        symbol_t * sym = op->resolved_symbol;
        if (sym->value.value)
        {
            *val = sym->value.value;
        }
        else if (sym->value.type_info && sym->value.type_info->kind == TD_KIND_PROC)
        {
            // Forward-declare: function not yet codegen'd (cross-package)
            *val = LLVMGetNamedFunction(ctx->module, sym->name);
            if (*val == NULL)
                *val = LLVMAddFunction(ctx->module, sym->name,
                                       sym->value.type_info->proc_metadata.func_type);
            sym->value.value = *val;
        }
        else
        {
            *val = NULL;
        }
        *cur_type = sym->value.type_info;
        return;
    }

### Fix C: Forward-declare in `ir_gen_postfix_call` Priority 3 (belt + suspenders)

**File**: `src/ir_gen_postfix.c:231-236`

When Priority 3 finds a valid proc type but `val` is still NULL, create a forward
declaration using the previous POSTFIX_MEMBER's `resolved_symbol`.

    // Priority 3: fall back to cur_type (e.g., function pointer calls)
    if (proc_type == NULL || proc_type->kind != TD_KIND_PROC)
    {
        if (*cur_type && (*cur_type)->kind == TD_KIND_PROC)
        {
            proc_type = *cur_type;
            // If val is NULL, create forward declaration from resolved symbol
            if (*val == NULL && node->list.count >= 2) {
                odin_grammar_node_t * ops = node->list.children[1];
                if (ops && i > 0) {
                    odin_grammar_node_t * prev = ops->list.children[i-1];
                    if (prev && prev->resolved_symbol) {
                        symbol_t * sym = prev->resolved_symbol;
                        if (sym->value.value)
                            *val = sym->value.value;
                        else {
                            *val = LLVMGetNamedFunction(ctx->module, sym->name);
                            if (*val == NULL)
                                *val = LLVMAddFunction(ctx->module, sym->name,
                                                       proc_type->proc_metadata.func_type);
                            sym->value.value = *val;
                        }
                    }
                }
            }
        }
    }

### Recommended Approach

**Fix A + Fix B together.** Fix A is the clean semantic fix — it propagates the
resolved symbol to the CALL node so the existing forward-declaration logic in
Priority 1 handles it uniformly. Fix B is a safety net that catches any other case
where a symbol's LLVM value isn't yet populated (e.g., future cross-package features).

Fix C is optional if Fix A is applied, but adds defense-in-depth.

### Testing

After applying fixes:

1. **Regression**: Run full test suite (`./tests/run_tests.sh`) — all 191 tests
   should still pass.
2. **Nested import**: Add `import "core:strings"` to `fmt.odin`, add `sbprint`
   function, verify it compiles and links.
3. **Direct cross-package**: Test file imports both `core:fmt` and `core:strings`,
   calls `strings.write_string` — should work.
4. **Multiple levels**: Create a 3-level chain (main → A → B → C), call `C.func()`
   from A's body.

### Future Improvement: Dependency-Aware Codegen Ordering

The root cause is that `ir_generate()` processes imports in insertion order rather
than topological dependency order. A more robust fix would:

1. Build a dependency graph: for each package, record which other packages it imports
2. Topologically sort `ctx->imports[]` so dependencies come before dependents
3. Or: do two passes — first pass registers all function declarations
   (`LLVMAddFunction`), second pass generates bodies

This eliminates the need for forward-declaration workarounds entirely and is the
correct long-term solution. However, Fix A + Fix B are sufficient for the immediate
problem and much simpler to implement.
