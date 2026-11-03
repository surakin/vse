# Phase 2.5 / Day 40 — Symbol Index & Markdown Emitter

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 39.

Produces the reference pages from the declaration trees.

## Tasks

- [x] Implement the module map reader (`docs/reference/modules.txt`) and file globbing via `os::`/libc directory APIs.
- [x] Build the **symbol index**: stable IDs, overload sets, source location, module, kind; serialize it (`docs/reference/symbols.txt`, plain line-based format) for the link checker.
- [x] Emit one Markdown page per module and an index page; deterministic ordering (declaration order within a header, headers in map order); anchors derived from symbol IDs.
- [x] Render: signature code block, summary, details, contract table, enumerator tables, "Defined in" header path, see-also links.
- [x] `docgen reference` writes `docs/reference/`; `docgen check` regenerates in memory and fails with a diff summary if the committed output is stale.
- [x] Idempotence + determinism tests (two runs byte-identical; independent of directory iteration order).
- [x] Golden-output tests from fixtures; unit test for the stale-detection path.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** `docgen reference` produces the reference pages for the fixtures byte-identically across runs, and `docgen check` detects a stale page.

## Result

New in `tools/docgen/src/`: `module_map.{h,cpp}` (map parser, glob matcher,
directory walk), `symbol_index.{h,cpp}` (IDs, anchors, `ResolveRef`, build
over all module headers), `markdown.{h,cpp}` (module pages, index page,
`symbols.txt`), `reference.{h,cpp}` (`BuildReference`, `WriteReference`,
`CheckReference`) and the CLI commands `docgen reference|check [--root]
[--modules] [--out] [--quiet]`. The extractor now records normalized parameter
types (`param_types`) for IDs; `ResolveInherited` gained a multi-root overload so
an override finds its documented base in another header.

Fixtures: `tests/docgen/fixtures/ref/` (a module map, demo headers, and the
`expected/` golden output of `docgen reference` on them plus the `good/`
corpus). `tests/docgen/reference_test.cpp`: 26 test cases (globbing, map
errors, IDs/anchors/resolution, rendering, goldens, determinism, write/check
staleness, `[review]` regressions); the suite is 113 cases and passes plain
and under ASan/UBSan.

`docs/reference/modules.txt` is the real module map. Running `docgen
reference` over the whole engine works (757 symbols, no errors, ~15 ms, and
`check` passes on the result), but the generated pages are **not committed
yet**: no engine header has `///` comments, so every page would say
"_Undocumented._". They are generated and committed after the retrofit (Day
44); Day 43 wires `check` into the build.

Decisions: references in comments are resolved while building the index (an
unresolved or ambiguous `[[Name]]` is an error at its line); among partially
qualified matches the one with the fewest scope components wins (so
`[[Counter]]` is the class, not its constructor); a function redeclared in
the same header is one symbol; link text uses the reference as written.

## Post-implementation code review (2026-11-04, `/code-review high`)

Fixed (each with a `[review]` regression test unless noted):
- Anchors were not injective: operators, destructors and pointer/reference
  overloads collided and were renumbered by declaration order → punctuation is
  spelled as words before collapsing (DESIGN.md section 5); reordering members
  no longer changes any anchor.
- `reference` deleted/`check` flagged any `*.md` that merely quoted the marker →
  only an exact marker line right after the title counts.
- A missing module map exited 1 instead of 2 → `io_failure` distinguishes it.
- A nested `--out` failed once per file → parents are created (`mkdir -p`),
  and a failure is reported once.
- The same-header redeclaration merge hid real duplicates and moved the
  location → it applies to functions only, keeps the first declaration, and
  takes only the comment from the documented one.
- `[[Hash::operator()]]` never resolved and hints over 512 characters were
  truncated → the hint is found after `operator()`/`operator[]` and compared
  in place, with no buffer.
- `CollectFiles` followed directory symlinks (cycles), ignored errors under the
  prefix, and had no depth cap → symlinked directories are not followed, depth
  is capped, and an unreadable directory is reported.
- Duplicated helpers → one `ArenaCat` and one `StableSort` in `diagnostics.h`;
  dead ternary in `Kind()` removed.
- STATUS.md and this file were not yet updated when the review ran → done here.
Also found while testing (not by the review): the module map path was kept as a
view of the caller's string (a dangling pointer in diagnostics) → copied into
the arena; `memcpy(…, nullptr, 0)` on empty views (UBSan) → `CopyBytes`.

Declined: `AssignAnchors`, `ResolveInheritance` and `ResolveRef` are quadratic
scans. Measured: all 757 symbols of the engine take ~15 ms end to end, so a
hash index would be speculative; revisit if a run exceeds a second.
