#include "polymorphism.h"

// Stage 1: detection only.
//
// Signature scanning: walk the AST produced by the `ProcedureSignature`
// grammar rule and detect any `AST_NODE_POLY_IDENT` node anywhere in the
// parameter list or return list.
//
// The ProcedureSignature node shape (per grammar at odin_grammar.gdl):
//   AST_NODE_PROCEDURE_SIGNATURE
//     children: [AST_NODE_CALLING_CONVENTION?, AST_NODE_RETURNS?,
//                AST_NODE_PARAMETER_LIST, AST_NODE_WHERE_CLAUSE?]
//
// AST_NODE_PARAMETER_LIST -> AST_NODE_PARAMETERS -> [AST_NODE_PARAMETER, ...]
// An AST_NODE_PARAMETER may itself contain AST_NODE_POLY_IDENT either:
//   * as the explicit tag (`(PolyIdent Colon)?` prefix), or
//   * as the shorthand type slot (the new `Identifier Colon PolyIdent`
//     alternative added to the Parameter rule).
//
// Detection: deep walk of all children originating at the signature node,
// stopping on the first AST_NODE_POLY_IDENT. Returns true iff at least one
// is found.

static bool
poly_walk_has_ident(odin_grammar_node_t const * node)
{
    if (node == NULL)
        return false;
    if (node->type == AST_NODE_POLY_IDENT)
        return true;
    for (size_t i = 0; i < node->list.count; i++)
    {
        if (poly_walk_has_ident(node->list.children[i]))
            return true;
    }
    return false;
}

bool
poly_signature_is_polymorphic(odin_grammar_node_t const * sig_node)
{
    // The signature node is wrapped in an outer AST_NODE_PROCEDURE_SIGNATURE
    // whose single child (in our odin_grammar.gdl construction) is the
    // AST_NODE_PROCEDURE_SIGNATURE itself — actually, looking at
    // sem_resolve_procedure_signature's loop:
    //     for i in children: if child->type == AST_NODE_PROCEDURE_SIGNATURE:
    //         iterate its children for CALLING_CONVENTION, RETURNS,
    //         PARAMETER_LIST, ...
    // So the wrapper may have a single child of type PROCEDURE_SIGNATURE
    // containing the actual signature pieces. To stay robust, treat the
    // caller's input as the signature root and walk recursively.
    return poly_walk_has_ident(sig_node);
}
