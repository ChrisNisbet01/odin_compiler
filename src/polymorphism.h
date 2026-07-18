#pragma once

// Polymorphic-procedure (generics) support for the Odin compiler.
//
// A polymorphic procedure is one whose signature contains one or more
// compile-time parameters prefixed with `$` (e.g. `$T: $T` declares a
// type-polymorphic parameter named T; `$N: int` declares an integer
// compile-time constant named N). Polymorphic procs are NOT analyzed or
// codegen'd directly. Instead, each call site instantiates a specialization
// by binding the $-parameters from the concrete argument types/values,
// cloning the proc AST with those bindings substituted, and analyzing &
// codegen'ing the clone as a regular procedure.
//
// This module owns:
//   * Detection of polymorphic signatures (`poly_signature_is_polymorphic`).
//   * (Stages 3+) Instantiation: clone + substitute + analyze + cache.
//   * (Stage 6)   Shared matching for polymorphic-overload-bundle candidates.

#include "odin_grammar_ast.h"

// True if the given procedure-signature node references ANY `$T`/`$N`
// polymorphic identifier (in parameters or returns). Used at the top-level
// registration site to decide whether to mark the proc symbol polymorphic.
bool poly_signature_is_polymorphic(odin_grammar_node_t const * sig_node);
