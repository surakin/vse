# Phase 2.5 / Day 39 — Doc-Comment Parser & Validation

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 38.

Parses the text of each doc comment into structured fields and validates it against the declaration it documents.

## Tasks

- [ ] Parse summary (first paragraph) and detail paragraphs; inline `` `code` ``, fenced blocks, `-` lists, `[[refs]]` kept as nodes.
- [ ] Parse contract labels (`Thread-safety:`, `Ownership:`, `Fails:`, `Aborts:`, `Returns:`, `Param name:`, `Example:`, `See:`, `Internal:`) with multi-line continuation.
- [ ] Validation against the declaration: `Param` names must match real parameters (typos error), `Returns:` on a `void` function warns, unknown labels error, duplicate labels error, empty summary errors.
- [ ] Defined behaviour for undocumented symbols (reported, not an error here: Day 43 lint decides).
- [ ] Tests: every label, continuation lines, malformed labels, parameter mismatch, inherited-docs rules (`override` may omit docs and inherit the base summary — decide and test).
- [ ] Round-trip test: parse -> re-emit normalized text is stable.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** doc comments on all fixtures parse into the expected structure and every validation rule has a passing and a failing test.
