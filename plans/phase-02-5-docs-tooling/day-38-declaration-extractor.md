# Phase 2.5 / Day 38 — Declaration Extractor

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 37.

Walks the token stream and produces a tree of declarations with their doc comments.

## Tasks

- [x] Define the declaration model (`Namespace`, `Class`, `Enum`, `Function`, `Variable`, `Alias`, `Macro`) holding qualified name, kind, access, signature text (normalized whitespace), template header, attributes, doc comment, source location.
- [x] Brace-depth walker with a scope stack: `namespace` (incl. nested `a::b` and anonymous), `class/struct/union`, `enum`/`enum class` (with underlying type and enumerators + per-enumerator docs), `extern "C"` blocks.
- [x] Access tracking: `public/protected/private` labels, struct-default-public, class-default-private; keep only the public surface per Day 36's rule.
- [x] Function-like declarations: free functions, methods, constructors/destructors, operators, conversion operators, `= default/delete`, `noexcept`, trailing return types, `[[nodiscard]]`/attributes, `constexpr/inline/static/virtual/override/final`, pure virtual.
- [x] Templates: `template<...>` headers on classes/functions/aliases, including requires-clauses; record the header text verbatim.
- [x] Variables and aliases: `static constexpr` members, `inline constexpr` namespace variables, `using X = ...`, `typedef`.
- [x] Skip bodies safely (inline function bodies, lambdas, initializers with braces) without losing scope depth; friend declarations ignored.
- [x] Unparseable constructs produce `file:line: error:` diagnostics (never a silent skip, never a guess); a `--keep-going` flag lists all.
- [x] Golden tests per fixture (declaration tree dumped to a stable text form); negative fixtures assert the exact diagnostics.
- [x] Run over all of `engine/**/*.h`: record the count per kind, fix scanner gaps, and commit the list of any constructs deliberately unsupported (should be empty or tiny).
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the extractor produces the correct public declaration tree for every fixture and every header in `engine/` with zero diagnostics.

## Result

`tools/docgen/src/extractor.{h,cpp}` (`Extract`, `DumpTree`, `Decl` model) and
`docgen dump [--all] [--keep-going] FILE.h`. Goldens: `tests/docgen/fixtures/good/*.tree`
(public surface only; `--all` adds hidden declarations tagged `[hidden]`).
`tests/docgen/extractor_test.cpp`: 26 test cases (fixtures, all engine
headers, forms, diagnostics, `[review]` regressions, mutation fuzz); the
suite is 57 cases in total and passes plain and under ASan/UBSan.

Run over all 43 `engine/**/*.h` headers: zero diagnostics. Declarations
found (hidden ones included): 72 namespace segments, 31 classes + 41 structs,
21 enums (6 `enum class`), 201 enumerators, 604 functions, 272 variables,
19 aliases. Constructs deliberately unsupported (all reported, none
guessed): macro-generated declarations, several declarators per statement,
declarators after a type definition; `Widget w(args);` is read as a function
(documented in DESIGN.md section 3). The engine headers use none of them.

Design notes: namespaces reopened in one file are merged; anonymous
structs/unions in a class contribute their members to it; out-of-line
definitions (`T Foo::Bar() {}`) add nothing; a requires-clause directly after
a template header is part of `template_header`.

## Post-implementation code review (2026-11-03, `/code-review high`)

Ten findings, all verified and fixed, each with a `[review]` regression test:
- Template-ids with function types (`std::function<void()> cb`) were taken
  for a parameter list → `<...>` after an identifier is skipped as a unit.
- `class ENGINE_API Foo {...}` fell through to the variable path and silently
  dropped the whole class → an all-caps export macro before the name is
  skipped (`struct stat st;` stays a variable).
- `int a, b;` / `struct S {...} s;` silently documented one name or none →
  now `unrecognized declaration` errors (documented in DESIGN.md).
- `const`/`noexcept` inside a trailing return type set member qualifiers →
  only recognized before `->`.
- Function-try-blocks: the `catch` handler was reported as a macro
  declaration → handlers are consumed with the body.
- `namespace a::b` gave the doc to `a` as well → innermost segment only.
- An unterminated class took the file's last token as its closing brace
  (stole a `///<`) → the closing brace is only used when it was consumed.
- `PrintDiagnostics`: unchecked `calloc` and a quadratic selection sort →
  stable insertion sort over a pointer array.
- `Join`/`Cat` grew arena builders repeatedly → sized once.
- `int v(5);` was a function → a literal first argument means a variable.
Declined: none.
