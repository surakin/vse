# Phase 2.5 / Day 37 — Source Scanner (Lexer)

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 36.

Turns a header into a token stream that is safe to walk: comments, strings and preprocessor lines can never be mistaken for declarations.

## Tasks

- [ ] Create `tools/docgen/` with CMake targets `docgen_lib`, `docgen` and a `tests/docgen/` Catch2 executable (production flags on the library, as for `jobs`).
- [ ] Implement the tokenizer: identifiers, numbers, punctuation incl. `::` `->` `<=>` `...`, line/column tracking.
- [ ] Skip or capture correctly: `//` and `/* */` comments, **string, char and raw-string literals** (with escapes and `R"delim(...)delim"`), line continuations, digit separators.
- [ ] Preprocessor lines: skip `#include`/`#if` families, but capture `#define NAME` lines as potential documented macros; track `#if 0` blocks as skipped.
- [ ] Doc comments: recognize `///` runs and `//!` runs (merging consecutive lines, dropping the marker and one leading space) and attach them to the following token as `pending_doc`.
- [ ] Diagnostics for malformed input (unterminated comment/string) with file:line.
- [ ] Tests from the fixtures: tokens, positions, tricky literals (a `//` inside a string, `/*` in a char literal, raw strings with `)` inside), `#if 0`, doc-comment attachment, CRLF files.
- [ ] Robustness: run the scanner over every header in `engine/` — zero diagnostics, deterministic output; fuzz-ish test truncating each header at random points must never crash or hang.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** every header in `engine/` scans with zero diagnostics, and truncation/garbage inputs fail cleanly rather than crash or loop.
