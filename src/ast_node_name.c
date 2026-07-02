#include "ast_node_name.h"

#include <stdio.h>

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
    case AST_NODE_IMPORT_USING:
        return "ImportUsing";
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
    case AST_NODE_AUTO_CAST_EXPR:
        return "AutoCastExpr";
    case AST_NODE_CAST_EXPR:
        return "CastExpr";
    case AST_NODE_TRANSMUTE_EXPR:
        return "TransmuteExpr";
    case AST_NODE_BIT_FIELD_FIELD:
        return "BitFieldField";
    case AST_NODE_BIT_FIELD_FIELD_LIST:
        return "BitFieldFieldList";
    case AST_NODE_STRUCT_FIELD:
        return "StructField";
    case AST_NODE_STRUCT_FIELD_LIST:
        return "StructFieldList";
    case AST_NODE_PRIMARY_EXPRESSION:
        return "PrimaryExpression";
    case AST_NODE_POSTFIX_CALL:
        return "PostfixCall";
    case AST_NODE_POSTFIX_SUBSCRIPT:
        return "PostfixSubscript";
    case AST_NODE_POSTFIX_SLICE:
        return "PostfixSlice";
    case AST_NODE_POSTFIX_SLICE_LT:
        return "PostfixSliceLt";
    case AST_NODE_POSTFIX_MEMBER:
        return "PostfixMember";
    case AST_NODE_POSTFIX_DEREF:
        return "PostfixDeref";
    case AST_NODE_POSTFIX_ASSERTION:
        return "PostfixAssertion";
    case AST_NODE_POSTFIX_OPS:
        return "PostfixOps";
    case AST_NODE_POSTFIX_EXPRESSION:
        return "PostfixExpression";
    case AST_NODE_UNARY_OP:
        return "UnaryOp";
    case AST_NODE_UNARY_EXPRESSION:
        return "UnaryExpression";
    case AST_NODE_MUL_OP:
        return "MulOp";
    case AST_NODE_MUL_EXPRESSION:
        return "MulExpression";
    case AST_NODE_ADD_OP:
        return "AddOp";
    case AST_NODE_ADD_EXPRESSION:
        return "AddExpression";
    case AST_NODE_SHIFT_OP:
        return "ShiftOp";
    case AST_NODE_SHIFT_EXPRESSION:
        return "ShiftExpression";
    case AST_NODE_BIT_AND_OP:
        return "BitAndOp";
    case AST_NODE_BIT_AND_EXPRESSION:
        return "BitAndExpression";
    case AST_NODE_BIT_XOR_OP:
        return "BitXorOp";
    case AST_NODE_BIT_XOR_EXPRESSION:
        return "BitXorExpression";
    case AST_NODE_BIT_OR_OP:
        return "BitOrOp";
    case AST_NODE_BIT_OR_EXPRESSION:
        return "BitOrExpression";
    case AST_NODE_COMP_OP:
        return "CompOp";
    case AST_NODE_COMP_EXPRESSION:
        return "CompExpression";
    case AST_NODE_LOG_AND_OP:
        return "LogAndOp";
    case AST_NODE_LOG_AND_EXPRESSION:
        return "LogAndExpression";
    case AST_NODE_LOG_OR_OP:
        return "LogOrOp";
    case AST_NODE_LOG_OR_EXPRESSION:
        return "LogOrExpression";
    case AST_NODE_RANGE_OP:
        return "RangeOp";
    case AST_NODE_RANGE_EXPRESSION:
        return "RangeExpression";
    case AST_NODE_TERNARY_EXPRESSION:
        return "TernaryExpression";
    case AST_NODE_OR_ELSE:
        return "OrElse";
    case AST_NODE_OR_RETURN:
        return "OrReturn";
    case AST_NODE_ASSIGN_OP:
        return "AssignOp";
    case AST_NODE_ASSIGN_EXPRESSION:
        return "AssignExpression";
    case AST_NODE_EXPRESSION:
        return "Expression";
    case AST_NODE_ARGUMENT_LIST:
        return "ArgumentList";
    case AST_NODE_COMPOUND_STATEMENT:
        return "CompoundStatement";
    case AST_NODE_DEFER_STATEMENT:
        return "DeferStatement";
    case AST_NODE_WHEN_STATEMENT:
        return "WhenStatement";
    case AST_NODE_IF_STATEMENT:
        return "IfStatement";
    case AST_NODE_FOR_STATEMENT:
        return "ForStatement";
    case AST_NODE_SWITCH_STATEMENT:
        return "SwitchStatement";
    case AST_NODE_SWITCH_CASE:
        return "SwitchCase";
    case AST_NODE_SWITCH_DEFAULT:
        return "SwitchDefault";
    case AST_NODE_RETURN_STATEMENT:
        return "ReturnStatement";
    case AST_NODE_BREAK_STATEMENT:
        return "BreakStatement";
    case AST_NODE_CONTINUE_STATEMENT:
        return "ContinueStatement";
    case AST_NODE_FALLTHROUGH_STATEMENT:
        return "FallthroughStatement";
    case AST_NODE_ASSIGN_STATEMENT:
        return "AssignStatement";
    case AST_NODE_EXPRESSION_STATEMENT:
        return "ExpressionStatement";
    case AST_NODE_PROCEDURE_LITERAL:
        return "ProcedureLiteral";
    case AST_NODE_VARIABLE_DECL:
        return "VariableDecl";
    case AST_NODE_CONSTANT_DECL:
        return "ConstantDecl";
    case AST_NODE_FOREIGN_IMPORT:
        return "ForeignImport";
    case AST_NODE_FOREIGN_BLOCK:
        return "ForeignBlock";
    case AST_NODE_WHEN_DECL:
        return "WhenDecl";
    case AST_NODE_WHEN_BODY:
        return "WhenBody";
    case AST_NODE_USING_DECL:
        return "UsingDecl";
    case AST_NODE_TOP_LEVEL_DECLARATION:
        return "TopLevelDeclaration";
    case AST_NODE_DIRECTIVE:
        return "Directive";
    case AST_NODE_DIRECTIVE_WITH_ARGS:
        return "DirectiveWithArgs";
    case AST_NODE_INTEGER_BASE:
        return "IntegerBase";
    case AST_NODE_INTEGER_VALUE:
        return "IntegerValue";
    case AST_NODE_FLOAT_BASE:
        return "FloatBase";
    case AST_NODE_FLOAT_VALUE:
        return "FloatValue";
    case AST_NODE_STRING_LITERAL:
        return "StringLiteral";
    case AST_NODE_RAW_STRING_LITERAL:
        return "RawStringLiteral";
    case AST_NODE_RUNE_LITERAL:
        return "RuneLiteral";
    case AST_NODE_BOOL_TRUE:
        return "BoolTrue";
    case AST_NODE_BOOL_FALSE:
        return "BoolFalse";
    case AST_NODE_NIL:
        return "Nil";
    case AST_NODE_NONE:
        return "None";
    case AST_NODE_LEN_EXPR:
        return "LenExpr";
    case AST_NODE_CAP_EXPR:
        return "CapExpr";
    case AST_NODE_SIZE_OF_EXPR:
        return "SizeOfExpr";
    case AST_NODE_ALIGN_OF_EXPR:
        return "AlignOfExpr";
    case AST_NODE_OFFSET_OF_EXPR:
        return "OffsetOfExpr";
    case AST_NODE_RAW_DATA_EXPR:
        return "RawDataExpr";
    case AST_NODE_MIN_EXPR:
        return "MinExpr";
    case AST_NODE_MAX_EXPR:
        return "MaxExpr";
    case AST_NODE_MAKE_EXPR:
        return "MakeExpr";
    case AST_NODE_NEW_EXPR:
        return "NewExpr";
    case AST_NODE_DELETE_EXPR:
        return "DeleteExpr";
    case AST_NODE_INCL_EXPR:
        return "InclExpr";
    case AST_NODE_EXCL_EXPR:
        return "ExclExpr";
    case AST_NODE_ELLIPSIS:
        return "Ellipsis";
    }
    return "Unknown";
}

static void
debug_print_ast_recursive(odin_grammar_node_t const * node, int depth)
{
    if (node == NULL)
    {
        printf("%*sNULL\n", depth * 2, "");
        return;
    }
    char const * name = get_node_type_name_from_node(node);
    char const * text = node->text ? node->text : "";
    size_t n = node->list.count;
    printf("%*s%s", depth * 2, "", name);
    if (text[0] != '\0')
        printf(" \"%s\"", text);
    printf(" (%zu children)\n", n);
    for (size_t i = 0; i < n; i++)
        debug_print_ast_recursive(node->list.children[i], depth + 1);
}

void
debug_print_ast(odin_grammar_node_t const * node)
{
    debug_print_ast_recursive(node, 0);
}
