## Completed
- **foreign blocks** (`foreign "lib" { ... }`) — Fully implemented: grammar, semantic analyser (pass 1 register + pass 2 type resolution), IR generator (emits `LLVMAddFunction` declarations). Tests pass.
- **Per-procedure `"c"` convention** — Calling convention parsing with quote stripping, string-to-ptr-u8 coercion at call sites, no context param prepended for C functions.
- **Top-level using** — Already parsed as AST_NODE_USING_DECL, semantic analyser and IR generator handle it. Tests pass.
- **`any` type / RTTI** — Added `type_id` field to `TypeDescriptor`, assigned unique IDs in `type_descriptor_alloc()`, stored source type's type_id when packing into `any` (variable init, assignment), added runtime type_id comparison with `llvm.trap` on mismatch in type assertion `x.(T)`.
- **Multiple return values** — Grammar + AST + actions fully wired. ProcMetadata already has return_count and returns[]. But get_or_create_proc_type explicitly ignores them (type_descriptors.c:478-479, (void)returns;). Need to: (a) plumb return list through proc type creation, (b) emit LLVM struct returns, (c) handle destructuring at call sites. Moderate because it touches sem, type descriptors, and IR gen.
- **auto_cast** — Unary prefix operator `auto_cast expr` that converts a value to the target type determined by context (variable type annotation, function return type). Grammar: `AutoCastExpr = KwAutoCast UnaryExpression @AST_ACTION_AUTO_CAST_EXPR`. Semantic analyser evaluates inner expression and returns NULL (type comes from context). Return type check is bypassed when the expression contains auto_cast. IR generator: `ir_gen_auto_cast_value` handles int↔int widening/narrowing, float↔float, int↔float, pointer↔int via LLVM cast opcodes. Target type is threaded through context (`auto_cast_target_type`) from variable declarations and return statements.

## Remaining features (ranked Easiest → Hardest)

Tier 2: Easy (infrastructure exists, limited scope)
1. **foreign import** (`foreign import lib "lib.so"`) — Parsed as AST_NODE_FOREIGN_IMPORT, both sem and IR handlers are no-ops. Would emit LLVM `declare external` with library linkage metadata, or produce linker flags.

Tier 3: Moderate
2. **when declaration** — Already parsed with full grammar+AST. Can reuse the when statement pattern (treat as runtime if at top level, llvm_ir_generator.c:3963). Harder than statement-when because it needs to handle top-level declarations inside conditional branches and register them in the symbol table.
5. **#load directive** — Needs file I/O at compile time. Grammar already handles it as AST_NODE_DIRECTIVE_WITH_ARGS. Would read a file into a string constant at compile time. Requires: (a) file reading during semantic analysis, (b) injecting the result as a string literal AST node, (c) handling expression context correctly.

Tier 4: Complex
6. **union type** — Grammar + AST + TD_KIND_UNION + is_type_node all in place. Needs: (a) get_or_create_union_type in type_descriptors.c (tagged union: {i64 tag, [union of fields]}), (b) semantic analyser to resolve fields and create the type, (c) IR generator to allocate with tag+payload and generate member accesses. The struct_members.h infrastructure (struct_or_union_members_st) can be reused.
7. **Type assertion x.(T) for unions** — Currently only works for any. For unions it needs: (a) the union type implemented (item 6), (b) runtime tag comparison against the requested variant, (c) field extraction from the union payload. Directly depends on item 6.

Tier 5: Very Complex
8. **context built-in** — Depends on implicit context parameter (item 9). Needs: (a) context type definition (allocator, logger struct), (b) context identifier resolving to the injected parameter, (c) field access on context.
9. **Implicit context parameter** — Architecture-wide change: inject ^context as hidden first parameter to every procedure, thread it through every call site. Zero existing infrastructure.
10. **Inline asm** — Keyword reserved, no grammar rule or AST node exists. Needs: (a) grammar rule for asm { ... }, (b) parsing of constraints (output/input/clobber), (c) LLVMConstInlineAsm integration. Well-bounded but significant new code.
11. **soa layout (Structure of Arrays)** — Grammar + AST + is_type_node all in place. But this is a major memory layout optimization affecting: type creation, member access GEP computation, array/slice indexing through SOA fields, and struct type representation. No existing infrastructure.

Tier 6: Architecture-Altering
12. **Polymorphic procedures / monomorphisation** — Full generics system. Complete template-like type parameter substitution, procedure instantiation with concrete types, duplicating LLVM functions per unique instantiation, caching. Zero infrastructure exists beyond the AST_NODE_POLY_IDENT parser token. By far the largest and most complex item.
