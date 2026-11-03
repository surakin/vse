# Phase 2.5 / Day 39 — Doc-Comment Parser & Validation

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 38.

Parses the text of each doc comment into structured fields and validates it against the declaration it documents.

## Tasks

- [x] Parse summary (first paragraph) and detail paragraphs; inline `` `code` ``, fenced blocks, `-` lists, `[[refs]]` kept as nodes.
- [x] Parse contract labels (`Thread-safety:`, `Ownership:`, `Fails:`, `Aborts:`, `Returns:`, `Param name:`, `Example:`, `See:`, `Internal:`) with multi-line continuation.
- [x] Validation against the declaration: `Param` names must match real parameters (typos error), `Returns:` on a `void` function warns, unknown labels error, duplicate labels error, empty summary errors.
- [x] Defined behaviour for undocumented symbols (reported, not an error here: Day 43 lint decides).
- [x] Tests: every label, continuation lines, malformed labels, parameter mismatch, inherited-docs rules (`override` may omit docs and inherit the base summary — decide and test).
- [x] Round-trip test: parse -> re-emit normalized text is stable.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** doc comments on all fixtures parse into the expected structure and every validation rule has a passing and a failing test.

## Result

`tools/docgen/src/doc_parser.{h,cpp}` (`ParseDocs`, `ParsedDoc`, `EmitDoc`,
`ResolveInherited`) and `docgen docs FILE.h`. The extractor gained what
validation needs: real parameter names and arity (`params`, `param_count`),
base-class names (`bases`), a returns-void flag, and a per-comment
`doc_claimed` bitmap. `tests/docgen/doc_parser_test.cpp`: 31 test cases; the
suite is 88 cases and passes plain and under ASan/UBSan. All 43 engine
headers still parse with zero diagnostics (they have no `///` comments yet).

Decisions (the "decide and test" items):
- **Label syntax:** a label is a capitalized word directly followed by `:` at
  column 0 (`Param` takes a name first). So prose that starts a line with
  `Word:` is read as a label — and an unknown one is an error. I changed the
  one fixture sentence that tripped this (`classes.h`).
- **Inherited docs:** an `override` without a comment inherits the base's by
  name + arity + constness, found through the base-class names (file-local
  today; Day 40's symbol index extends it across files). Such an override is
  not "undocumented". With a comment of its own, that comment wins.
- **Undocumented symbols:** collected in `DocResult::undocumented` (public,
  not a namespace, no `///`/`///<`, no inherited doc). Not an error here.
- **New diagnostics** (catalogued in DESIGN.md): `empty label`, `invalid
  thread-safety category`, `malformed label`, `malformed reference`,
  `unterminated code fence`, `doc comment is not attached to a declaration`,
  and warnings for a summary over 160 characters or without a final period.
- A comment on a forward declaration, out-of-line definition or `friend` is
  accepted and ignored; on `using namespace`, `static_assert` and the like it
  is an error.

## Post-implementation code review (2026-11-04, `/code-review high`)

Nine findings, all verified and fixed, each with a `[review]` regression test:
- Override matching compared *named* parameter counts → the real arity is now
  recorded (`param_count`).
- Pointer-to-member parameters (`void (S::*pm)(int)`) had no name → any
  parenthesized declarator containing `*`/`&` and followed by `(`/`[` is a
  function-pointer/array-reference declarator.
- Documented forward declarations, out-of-line definitions and friends got
  "not attached to a declaration" → claimed by the extractor.
- `int const` (unnamed) recorded `const` as the name → cv-qualifier rules
  (`const T` is a type, `int const x` has a name).
- `ResolveIn` recursed twice per level → once.
- GNU named variadic macro parameters (`y...`) and a cut-off `#define` →
  dots stripped; the missing `)` no longer truncates the last name.
- `Param:` with no name → `malformed label` error.
- A `[[reference]]` wrapped over two comment lines was an error → references
  are scanned over the joined text, with errors still reported on the line
  where the reference starts.
- `docgen docs` was missing from the usage text and DESIGN.md → added.
Declined: none.
