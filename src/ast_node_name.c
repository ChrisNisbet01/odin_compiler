#include "ast_node_name.h"

char const *
get_node_type_name_from_node(odin_grammar_node_t const * node)
{
    if (node == NULL)
        return "NULL";
    return get_node_type_name_from_type(node->type);
}

char const *
get_node_type_name_from_type(odin_grammar_node_type_t node_type)
{
    switch (node_type)
    {
    case AST_NODE_PROGRAM:
        return "Program";
    case AST_NODE_EXTERNAL_DECLARATIONS:
        return "ExternalDeclarations";
    case AST_NODE_PACKAGE_CLAUSE:
        return "PackageClause";
    case AST_NODE_IMPORT:
        return "Import";
    case AST_NODE_IMPORT_NAMED:
        return "ImportNamed";
    case AST_NODE_IDENTIFIER:
        return "Identifier";
    case AST_NODE_POLY_IDENT:
        return "PolyIdent";
    case AST_NODE_BASIC_TYPE:
        return "BasicType";
    case AST_NODE_POINTER_TYPE:
        return "PointerType";
    case AST_NODE_ARRAY_TYPE:
        return "ArrayType";
    case AST_NODE_DYNAMIC_ARRAY_TYPE:
        return "DynamicArrayType";
    case AST_NODE_SLICE_TYPE:
        return "SliceType";
    case AST_NODE_MAP_TYPE:
        return "MapType";
    case AST_NODE_SOA_TYPE:
        return "SoaType";
    case AST_NODE_ENUM_TYPE:
        return "EnumType";
    case AST_NODE_ENUM_TYPE_REF:
        return "EnumTypeRef";
    case AST_NODE_UNION_TYPE:
        return "UnionType";
    case AST_NODE_BIT_FIELD_TYPE:
        return "BitFieldType";
    case AST_NODE_BIT_SET_TYPE:
        return "BitSetType";
    case AST_NODE_BIT_SET_RANGE:
        return "BitSetRange";
    case AST_NODE_STRUCT_TYPE:
        return "StructType";
    case AST_NODE_STRUCT_TYPE_REF:
        return "StructTypeRef";
    case AST_NODE_TYPE_NAME:
        return "TypeName";
    case AST_NODE_DISTINCT_TYPE:
        return "DistinctType";
    case AST_NODE_PROCEDURE_SIGNATURE:
        return "ProcedureSignature";
    case AST_NODE_PARAMETER:
        return "Parameter";
    case AST_NODE_PARAMETERS:
        return "Parameters";
    case AST_NODE_PARAMETER_LIST:
        return "ParameterList";
    case AST_NODE_NAMED_RETURN:
        return "NamedReturn";
    case AST_NODE_RETURN_LIST:
        return "ReturnList";
    case AST_NODE_RETURN_TYPE_LIST:
        return "ReturnTypeList";
    case AST_NODE_RETURNS:
        return "Returns";
    case AST_NODE_WHERE_CLAUSE:
        return "WhereClause";
    case AST_NODE_CALLING_CONVENTION:
        return "CallingConvention";
    case AST_NODE_CONTEXT_EXPR:
        return "ContextExpr";
    case AST_NODE_TRIPLE_DASH:
        return "TripleDash";
    case AST_NODE_IDENTIFIER_LIST:
        return "IdentifierList";
    case AST_NODE_ENUMERATOR:
        return "Enumerator";
    case AST_NODE_ENUMERATOR_LIST:
        return "EnumeratorList";
    case AST_NODE_UNION_FIELD:
        return "UnionField";
    case AST_NODE_UNION_FIELD_LIST:
        return "UnionFieldList";
    case AST_NODE_BIT_FIELD_FIELD:
        return "BitFieldField";
    case AST_NODE_BIT_FIELD_FIELD_LIST:
        return "BitFieldFieldList";
    case AST_NODE_STRUCT_FIELD:
        return "StructField";
    case AST_NODE_STRUCT_FIELD_LIST:
        return "StructFieldList";
    }
    return "Unknown";
}
