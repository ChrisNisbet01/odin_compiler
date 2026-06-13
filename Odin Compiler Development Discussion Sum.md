Odin Compiler Development Discussion Summary
=============================================

1. PARSING COMPARISON: ODIN VS C
--------------------------------
- Odin is drastically easier to parse than C due to a clean, context-free grammar.
- C is context-sensitive and suffers from the 'lexer hack' (typedef name status problem). 
  For example, `AA * bb;` is ambiguous in C without a symbol table lookup.
- Odin fixes this using explicit colon-based declaration syntax (`name: type`), making it context-free.
- C type casting (`(foo) - bar;`) is ambiguous; Odin enforces explicit casting (`cast(i32)bar` or `i32(bar)`).
- C declaration syntax uses the confusing 'Clockwise/Spiral Rule'. Odin types read strictly from left to right (e.g., `p: ^[4]int`).
- Odin eliminates parentheses around control flow conditions but makes the block braces `{}` or `do` keyword mandatory, avoiding the dangling-else problem entirely.

2. CLASSES AND DATA-ORIENTED DESIGN
------------------------------------
- Odin does not support classes or object-oriented programming.
- Data and methods are fully separated: structs contain only fields, and external procedures operate on them.
- Inheritance is replaced by struct embedding using the `using` keyword to achieve subtype polymorphism cleanly.
- Function overloading is disallowed to maintain explicit readability; polymorphism is achieved via explicit generics or tagged unions.
- Encapsulation is package-based using the `@(private)` attribute, rather than class-based (`private`/`public`).

3. MEMORY ALLOCATION SCHEMES
----------------------------
- Stack Allocation (Default): e.g., `v := MyStruct{}` (automatic cleanup).
- Heap Allocation: Utilises the built-in Context system (`context.allocator`).
  - Single Instance: `v_ptr := new(MyStruct)` (explicit cleanup with `free(v_ptr)`).
  - Multiple Instances / Slices: `slice := make([]MyStruct, count)` (explicit cleanup with `delete(slice)`).
- Custom Allocators: Odin permits seamlessly overriding `context.allocator` with custom performance allocators, such as an Arena Allocator, to prevent OS-level overhead and memory fragmentation.

4. THE STANDARD LIBRARY ARCHITECTURE
-------------------------------------
- The standard library ships embedded within the root Odin installation directory under the `core` folder.
- Compilation is package-based. The compiler must parse all `.odin` files inside a given directory simultaneously as a unified AST block.
- Interactivity with the host OS/C library is managed via `foreign` blocks (e.g., `foreign import libc "system:c"`), mapping external symbols directly using attributes like `@(link_name="...")`.
- Critical bootstrap order: `base:runtime` (implicit context setup) -> `core:os` (file system/system calls) -> `core:fmt` (formatting/printing).

5. LOWERING SEMANTICS TO LLVM IR (WITH A C-BASED FRONTEND)
---------------------------------------------------------
- Implicit Context: Almost every procedure expects a pointer to the tracking context as its implicit hidden first argument. In LLVM IR, append `%Runtime_Context*` to parameter signatures.
- String & Slice Representation: Lowered as explicit 2-tuple structures containing a data pointer and an integer length (e.g., `%string = type { i8*, i64 }`).
- The `any` Type: Lowered as a structural pair containing a raw data pointer and a unique runtime type information identifier (`%any = type { i8*, i64 }`). Requires an RTTI lookup strategy.
- Multiple Return Values: Mapped neatly using first-class anonymous structures (`ret { %string, i1 }`) packed and unpacked via `insertvalue` and `extractvalue`.
- Symbol Resolution & Shadowing: Handle name collisions by executing a breadth-first lookup pass on nested or embedded structures. Fields in the outermost struct shadow deeper elements.
- Runtime Type Decisions:
  - Unions: Represented as an aggregated data block padded to the largest member's size, alongside an integer tracking tag to evaluate via LLVM `switch`.
  - `any`: Evaluated dynamically by comparing the global type identity against assigned type IDs using LLVM `icmp eq`.
- Type Punning: Realised natively via `transmute` (lowered to LLVM `bitcast` for identical sizes) or via explicit pointer reinterpretation using standard pointer bitcasts and memory instructions.
