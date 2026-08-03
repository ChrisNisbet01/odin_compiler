# `#+build ignore` — implementation plan (tracking)

## Status: DONE — all 241 tests pass (236 baseline + 5 new)

Implemented per this plan. The `#+build` prefix matches the official
`FileTag = "#+" IDENT` shape (`lexeme("#+") lexeme("build" IdBoundary)`);
`BuildTag`/`BuildDirective` produce dedicated AST nodes;
`ast_file_has_build_ignore()` is shared by the main-file check (guarded by
`import_reg_depth == 0`) and package resolution (single-file and directory
imports); the import-codegen loop skips `pkg->build_ignored`.

Goal: robust, official `#+build` build-directive support (tags comma-separated,
`!`/`not` negation accepted), replacing the current proof-of-concept that folds
`#+build` into `DirectiveWithArgs` and matches by exact text.

## Critical parsing constraint (empirical, from prior sessions)

- Do **NOT** write `lexeme("#+build" ...)` — plain `lexeme()` wrapping of the
  `#+build` string has failed twice ("Parse Error… Found: +").
- Use the **proven** form: bare `"#"` string followed by `lexeme("+build", ws)`:
  `"#" lexeme("+build", ws) ...` — the `ws` control flag means
  `EPC_CONSUME_WS` only, and the `#` is never inside a lexeme.
- Note: current easy_pc (commit f480e62) defines
  `EPC_CONSUME_ALL = (WS | C_COMMENT | CPP_COMMENT)` (bash bit moved to
  `EPC_CONSUME_ALL_STYLES`), so plain `lexeme()` *should* no longer eat `#` —
  but we keep the proven pattern anyway; it works and avoids re-litigating.

## RESOLVED (session experiment, 2026-08-04)

The `, ws` flags were NOT actually needed. The old `EPC_CONSUME_ALL` (up to
commit afa33b6) included `EPC_CONSUME_BASH_COMMENT`, so `lexeme("#+build")`
ate the whole line as a `#` comment ("Found: +"). Current easy_pc (f480e62)
removed the bash bit from `EPC_CONSUME_ALL`, so `lexeme("#+build" IdBoundary)`
parses fine. Verified empirically: with the `ws` flags dropped and the prefix
as `lexeme("#+") lexeme("build" IdBoundary)`, all 5 build-ignore behaviors and
the full 241-test suite pass. The final grammar mirrors the official EBNF
`FileTag = "#+" IDENT` — `#+` is a distinct pair, followed by the `build`
IDENT (with `IdBoundary` so `#+buildx` is a parse error rather than tag `x`).

## Grammar (src/odin_grammar.gdl)

Replace the build-directive alternative folded into `DirectiveWithArgs` (line
192) with dedicated rules:

```gdl
BuildTag = lexeme(UnaryOpNot? identifier) @AST_ACTION_BUILD_TAG;
BuildTags = delimited_flex(BuildTag, Comma) Comma?;
BuildDirective = lexeme("#+") lexeme("build" IdBoundary) BuildTags @AST_ACTION_BUILD_DIRECTIVE;
```

- Revert `DirectiveWithArgs` to the bracket-only form:
  `DirectiveWithArgs = lexeme("#" DirectiveName) LBracket Expression RBracket @AST_ACTION_DIRECTIVE_WITH_ARGS;`
- Hook `BuildDirective` into `TopLevelDeclaration` **only**
  (before `DirectiveWithArgs`), NOT into `SoaType` (line 317) or
  `PrimaryExpression` (line 419) where the current change leaked it.
- `BuildTags` has no action so tag nodes flatten into `BuildDirective`
  (easy_pc AST builder flattens action-less rules).
- `IdBoundary` after `build` rejects `#+buildx` at parse time (verified).

## AST (new nodes)

- `AST_NODE_BUILD_DIRECTIVE`, `AST_NODE_BUILD_TAG` in `src/odin_grammar_ast.h`
  (~line 112, after `AST_NODE_DIRECTIVE_WITH_ARGS`).
- `odin_grammar_ast_actions.c`:
  - `DEFINE_ACTION(ast_action_build_directive_action, AST_NODE_BUILD_DIRECTIVE, true)`
    (model: `ast_action_directive_action`).
  - `DEFINE_ACTION(ast_action_build_tag_action, AST_NODE_BUILD_TAG, true)`
    (lexeme-wrapped rule, so plain DEFINE_ACTION, not terminal).
  - `REGISTER(AST_ACTION_BUILD_DIRECTIVE, ...)` / `REGISTER(AST_ACTION_BUILD_TAG, ...)`.
- `src/ast_node_name.c` (~line 244): add the two case labels. Build runs
  `-Wswitch-enum`, so add cases to any other exhaustive switch the build flags.

## Shared predicate (src/ast_utils.c / ast_utils.h)

```c
bool ast_file_has_build_ignore(odin_grammar_node_t * program_ast);
```

Walk `PROGRAM -> EXTERNAL_DECLARATIONS -> children`; for each
`AST_NODE_BUILD_DIRECTIVE`, scan grandchildren for an
`AST_NODE_BUILD_TAG` whose `text == "ignore"`. Replaces both the
`should_ignore_build` scan in semantic_analyser.c and the `#build[ignore]`
text match in package_resolver.c.

## Detection / flag fixes

1. `src/semantic_analyser.c`:
   - Delete `should_ignore_build` (incl. debug `fprintf` loop) — call
     `ast_file_has_build_ignore` instead.
   - Flag-clobber fix: only set `ctx->build_ignored` for the main file, i.e.
     guard `ctx->import_reg_depth == 0` before assigning; a build-ignored
     *import* must not disable compilation of the main file.
2. `src/package_resolver.c` (~line 580-601): replace the `#build[ignore]`
   text-match block with `pkg->build_ignored = ast_file_has_build_ignore(pkg->ast);`.
3. `src/llvm_ir_generator.c` (~line 3918): add `|| pkg->build_ignored` to the
   import-codegen loop skip condition (currently only
   `pkg == NULL || pkg->ast == NULL || pkg->codegen_done`).

## Tests (tests/)

- `test_build_ignore.odin` — keep `#+build ignore`; must still produce a dummy
  main and run (exit 0).
- `test_build_ignore_ws.odin` — `#+build   ignore` (extra whitespace).
- `test_build_ignore_tags.odin` — `#+build windows, ignore, linux`
  (comma-separated multi-tag).
- `test_build_ignore_import.odin` + helper pkg dir with `#+build ignore` —
  main imports it, still compiles and runs.
- `expected_to_fail/test_build_not_ignored.odin` — `#+build windows` on a file
  whose code does NOT compile; proves we do not over-ignore.

## Verification

1. `cmake --build build -j4` (regen + compile; check no `-Wswitch-enum` warnings).
2. `bash tests/run_tests.sh` — baseline 236 tests + new ones, all green.
3. Sanity: `./build/src/odinc build --file tests/test_build_ignore.odin`
   produces an executable that exits 0.
