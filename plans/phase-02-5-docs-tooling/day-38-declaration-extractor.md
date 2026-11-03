# Phase 2.5 / Day 38 — Declaration Extractor

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 37.

Walks the token stream and produces a tree of declarations with their doc comments.

## Tasks

- [ ] Define the declaration model (`Namespace`, `Class`, `Enum`, `Function`, `Variable`, `Alias`, `Macro`) holding qualified name, kind, access, signature text (normalized whitespace), template header, attributes, doc comment, source location.
- [ ] Brace-depth walker with a scope stack: `namespace` (incl. nested `a::b` and anonymous), `class/struct/union`, `enum`/`enum class` (with underlying type and enumerators + per-enumerator docs), `extern "C"` blocks.
- [ ] Access tracking: `public/protected/private` labels, struct-default-public, class-default-private; keep only the public surface per Day 36's rule.
- [ ] Function-like declarations: free functions, methods, constructors/destructors, operators, conversion operators, `= default/delete`, `noexcept`, trailing return types, `[[nodiscard]]`/attributes, `constexpr/inline/static/virtual/override/final`, pure virtual.
- [ ] Templates: `template<...>` headers on classes/functions/aliases, including requires-clauses; record the header text verbatim.
- [ ] Variables and aliases: `static constexpr` members, `inline constexpr` namespace variables, `using X = ...`, `typedef`.
- [ ] Skip bodies safely (inline function bodies, lambdas, initializers with braces) without losing scope depth; friend declarations ignored.
- [ ] Unparseable constructs produce `file:line: error:` diagnostics (never a silent skip, never a guess); a `--keep-going` flag lists all.
- [ ] Golden tests per fixture (declaration tree dumped to a stable text form); negative fixtures assert the exact diagnostics.
- [ ] Run over all of `engine/**/*.h`: record the count per kind, fix scanner gaps, and commit the list of any constructs deliberately unsupported (should be empty or tiny).
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the extractor produces the correct public declaration tree for every fixture and every header in `engine/` with zero diagnostics.
