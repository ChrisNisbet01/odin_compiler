# Qualified Type Name Parser Support

## Status: WORKING

The `QualifiedTypeName` grammar rule (`pkg.Type` syntax) is now working correctly.

## Implementation

### Grammar (`odin_grammar.gdl`)
```
QualifiedTypeName = Identifier Dot Identifier @AST_ACTION_QUALIFIED_TYPE_NAME;
```

### AST Node (`odin_grammar_ast.h`)
```c
AST_NODE_QUALIFIED_TYPE_NAME,
```

### Semantic Resolution (`sem_type_resolver.c`)
The `sem_resolve_qualified_type_name` function:
1. Extracts the package name and type name from children
2. Looks up the package by name in the import list
3. Finds the type symbol in the package's scope
4. Returns the resolved `TypeDescriptor`

### Usage
```odin
import "core:strings"

my_func :: proc(b: ^strings.Builder) {
    b.count = 0
}
```

## Notes
- Requires the package to be imported (`import "core:strings"`)
- The package name must match the `package` clause in the imported file
- Works in type positions (function parameters, variable declarations, etc.)