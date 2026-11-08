# Phase 3 — Documentation Tooling (`docgen`)

**Depends on:** Phase 2 (nothing else; it is deliberately small and sits
between Phase 2 and Phase 4 so documentation rules are enforceable from the
RHI onward). Numbered 2.5 to avoid renumbering the roadmap.
**Goal:** our own documentation tool, written in C++ on the foundation
layer (decision, 2026-11-03: no Doxygen, no libclang). It reads `///` doc
comments from public headers with a purpose-built scanner, generates the
API reference as Markdown, resolves `[[Symbol]]` cross-references, checks
every link, compiles the code samples in the documentation, and lints
documentation coverage and contract labels.
**Definition of done:** `docgen` generates `docs/reference/` for all of
Phases 0-2 with 100% public-symbol coverage; `ctest` runs `docs_check`,
`docs_links`, `docs_samples` and `docs_lint`, and each fails when its
class of error is introduced.

## Design decisions (settled 2026-11-03)

- **Language:** C++20 on our own foundation (strings, UTF-8, `os::` file
  I/O, arenas), under `tools/docgen/`. A dev-time tool: never linked into
  the engine or a game.
- **Parsing:** a purpose-built scanner/declaration extractor, not a full C++
  parser and not libclang. It handles this codebase's style, refuses loudly
  on anything it cannot parse, and never guesses.
- **Authoring format:** Markdown for guides/tutorials; `///` comments in
  headers for the reference (see `docs/DOC-CONVENTIONS.md`).
- **Generated reference is committed** and checked for staleness.

## Days

Day numbering continues from Phase 2 (the next phase's days follow Day 44).

36. [Design Spec & Doc-Comment Convention](phase-03-docs-tooling/day-36-design-and-convention.md)
37. [Source Scanner (Lexer)](phase-03-docs-tooling/day-37-scanner.md)
38. [Declaration Extractor](phase-03-docs-tooling/day-38-declaration-extractor.md)
39. [Doc-Comment Parser & Validation](phase-03-docs-tooling/day-39-doc-comment-parser.md)
40. [Symbol Index & Markdown Emitter](phase-03-docs-tooling/day-40-symbol-index-and-markdown.md)
41. [Cross-References & Link Checker](phase-03-docs-tooling/day-41-crossrefs-and-link-check.md)
42. [Sample Compiler](phase-03-docs-tooling/day-42-sample-compiler.md)
43. [Lint, Coverage & Build Integration](phase-03-docs-tooling/day-43-lint-coverage-and-build.md)
44. [Retrofit Existing Headers & Adopt the Rule](phase-03-docs-tooling/day-44-header-retrofit.md)

## Notes / open questions

- Doc-comment inheritance for `override`s and the exact public/internal
  rules are pinned down on Days 36 and 39.
- Static HTML rendering and in-editor help are out of scope here; the
  symbol index and Markdown output are designed so Phase 27's help viewer
  or a later site generator can consume them.
- Fuzzy risks: templates/requires-clauses and macro-heavy code are the
  likeliest scanner gaps; Day 38 runs over every real header to flush them
  out early.
