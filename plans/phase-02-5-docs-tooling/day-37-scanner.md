# Phase 2.5 / Day 37 — Source Scanner (Lexer)

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 36.

Turns a header into a token stream that is safe to walk: comments, strings and preprocessor lines can never be mistaken for declarations.

## Tasks

- [x] Create `tools/docgen/` with CMake targets `docgen_lib`, `docgen` and a `tests/docgen/` Catch2 executable (production flags on the library, as for `jobs`).
- [x] Implement the tokenizer: identifiers, numbers, punctuation incl. `::` `->` `<=>` `...`, line/column tracking.
- [x] Skip or capture correctly: `//` and `/* */` comments, **string, char and raw-string literals** (with escapes and `R"delim(...)delim"`), line continuations, digit separators.
- [x] Preprocessor lines: skip `#include`/`#if` families, but capture `#define NAME` lines as potential documented macros; track `#if 0` blocks as skipped.
- [x] Doc comments: recognize `///` runs and `//!` runs (merging consecutive lines, dropping the marker and one leading space) and attach them to the following token as `pending_doc`.
- [x] Diagnostics for malformed input (unterminated comment/string) with file:line.
- [x] Tests from the fixtures: tokens, positions, tricky literals (a `//` inside a string, `/*` in a char literal, raw strings with `)` inside), `#if 0`, doc-comment attachment, CRLF files.
- [x] Robustness: run the scanner over every header in `engine/` — zero diagnostics, deterministic output; fuzz-ish test truncating each header at random points must never crash or hang.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** every header in `engine/` scans with zero diagnostics, and truncation/garbage inputs fail cleanly rather than crash or loop.

## Post-implementation code review (2026-11-03, `/code-review high`)

Found and fixed (each has a regression test tagged `[review]`; the reviewer
reproduced most of them against the first implementation):
- `#define` text swallowed trailing comments, losing a `///<` doc on a macro
  → directive bodies are now scanned comment-aware (strings, `//`, `/* */`
  across lines, continuations); a single-line `#define X 1 ///< doc`
  attaches the trailing doc to the macro.
- `#if 0 // reason` / `#if 0 /* x */` weren't recognized as skipped
  regions → the condition is compared after comments are stripped; the same
  comment-aware scan is used inside skipped regions (`#endif /* ... */`).
- A UTF-8 BOM became an identifier token and defeated `#` directive
  detection → skipped at start of input.
- Char literals mishandled backslash-newline (LF and CRLF) → now match
  string literals.
- `0xE+1` lexed as one number → exponent signs follow `e/E` only for
  decimal literals and `p/P` only for hex.
- Columns after line continuations (outside and inside directives) used a
  stale line start → fixed via a shared `NewlineInside` helper.
- `last_token_on_line_` was stale after a multi-line block comment or raw
  string, so a following `///` was misread as trailing → reset by the same
  helper.
- Raw-string delimiter limit off by one (accepted 17) → 16.
- Missing `<algorithm>` include in the test; STATUS/day file updated.
Added beyond the review: an ASan/UBSan flavor (`docgen_lib_asan`,
`docgen_tests_asan`) because the scanner reads arbitrary bytes — the
truncation/junk fuzz tests run clean under it.
Declined: none. Known limit (documented): the scanner is intentionally not
a preprocessor — both branches of `#if/#else` are scanned except `#if 0`.
