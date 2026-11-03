# Phase 2.5 / Day 41 — Cross-References & Link Checker

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 40.

Makes the docs refuse to rot: broken references fail the build.

## Tasks

- [x] Resolve `[[Symbol]]` references in doc comments and in `docs/**/*.md` against the symbol index (qualified, partially qualified, overload hints); rewrite to Markdown links in generated output.
- [x] Errors for unresolved or ambiguous references with the candidate list.
- [x] Markdown link checker for `docs/**` and the repo's top-level `.md` files: relative file links, `#anchor` links (including generated heading anchors), image paths; external `http(s)` links are listed but not fetched.
- [x] Orphan-page report (pages not reachable from `docs/README.md`).
- [x] `docgen links` command; exit non-zero on any error.
- [x] Tests: resolved/unresolved/ambiguous refs, anchor generation matching the emitter, orphan detection, and a run over the real `docs/` tree.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** `docgen links` is clean on the real docs tree and each class of broken reference has a failing test proving it is caught.

## Result

New: `tools/docgen/src/md_scan.{h,cpp}` (Markdown scanner shared with the
Day 42 sample compiler: fences with info string/body/lines, headings and
anchors, links, images, `[[refs]]`), `links.{h,cpp}` (`CheckLinks`), the
`docgen links` command, `ResolveRef` candidate lists plus `RefProblem`
(ambiguous references name their candidates), and
`tests/docgen/links_test.cpp` (21 cases; suite is 134 cases, plain and
ASan/UBSan). The real tree is clean: 25 files, 43 links, ~15 ms.

Decisions: `[[refs]]` in hand-written Markdown are validated but stay as
written (only generated output carries rewritten links); orphans are errors
(the three design specs and `DOC-CONVENTIONS.md` were not linked from
`docs/README.md`, which the first real run found, so it now links them);
the scanner is deliberately not CommonMark (see DESIGN.md section 10).

## Post-implementation code review (2026-11-04, `/code-review high`)

Fixed, each with a `[review]` regression test:
- `docgen links` used a possibly partial index silently when headers had
  errors → the errors are printed and fail the run.
- Missing `docs/README.md` disabled the orphan check silently → error.
- Footnote definitions `[^1]: text` were read as link definitions → skipped.
- Only four URI schemes were external (`tel:`, `data:` were "broken files")
  → any `scheme:` is external.
- `<a id>` anchors were collected inside code fences and from `data-id=` →
  taken only outside fences, on attribute boundaries.
- `x[i][j]` / `[text][label]` prose produced "undefined link label" → that
  check is dropped (CommonMark renders such text literally).
- Code spans and `[[refs]]` that wrap across lines were mis-scanned → the
  scanner works per paragraph; wrapped refs lose the line break.
- `%00` in a link decoded to a NUL and truncated the stat() path → stays
  literal; removed a dead `+ 0`.
- Missing `<cstring>` include; `RefProblem` copied its result needlessly.

Declined: `DropAll` pops one element at a time (no `Clear()` on `Array`; the
candidate list is tiny and only filled when the caller asks for it).

The regression tests were written against the reviewer's reproductions; I
did not re-run each against the unfixed code.
