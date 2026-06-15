# Unimplemented / Partially Implemented Core Features

## Not Implemented
- **`defer` statement codegen** – Parsed and semantically analysed, but no LLVM IR generation (`ir_gen_defer_statement` missing).
- **`or_else` / `or_return` statements** – No AST node handling, semantic analysis, or codegen.
- **`when` compile‑time branch** – No support.
- **`any` type / RTTI** – No type descriptor or runtime support.
- **Tagged unions / `switch type`** – No support.
- **Polymorphic procedures / monomorphisation** – Generics not implemented.
- **Built‑in procedures** (`make`, `new`, `delete`, `len`, `cap`, `append`) – No intrinsic handling.
- **`transmute` / `cast`** – No support.
- **`bit_field`, `bit_set`, `map` type codegen** – Only basic type descriptors exist; no lowering to LLVM.
- **`foreign` blocks / external linking** – No support.
- **`soa` (structure‑of‑arrays) layout** – Not implemented.
- **Implicit context parameter (`$`)** – No hidden parameter insertion on procedures.
- **Multiple return values** – Procedures only support a single return type; tuple returns not handled in IRgen (only single‑value `LLVMBuildRet`).
- **`using` on parameters** – Grammar allows it but no detection or handling.

## Partially Implemented
- **Chained struct member access** – `using` field promotion works for single‑level access (e.g., `v.x`), but chained access (e.g., `v.inner.x`) fails because the rvalue path loads intermediate struct values, breaking subsequent GEP chains. This is a pre‑existing limitation affecting all nested member access, not just `using`.
- **Nested procedures as values** – Procedure literals can appear anywhere expressions are allowed (including inside other procs), but there is no dedicated support for nested proc *declarations* as symbols; they are treated as constant/proc‑value declarations, which works via existing variable/constant handling.
- **`defer` statement semantics** – The statement is recognised in the semantic analyser (passed to `sem_pass2_node`), but no codegen is emitted, so it currently does nothing.
