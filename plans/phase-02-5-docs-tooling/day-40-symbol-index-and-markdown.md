# Phase 2.5 / Day 40 — Symbol Index & Markdown Emitter

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 39.

Produces the reference pages from the declaration trees.

## Tasks

- [ ] Implement the module map reader (`docs/reference/modules.txt`) and file globbing via `os::`/libc directory APIs.
- [ ] Build the **symbol index**: stable IDs, overload sets, source location, module, kind; serialize it (`docs/reference/symbols.txt`, plain line-based format) for the link checker.
- [ ] Emit one Markdown page per module and an index page; deterministic ordering (declaration order within a header, headers in map order); anchors derived from symbol IDs.
- [ ] Render: signature code block, summary, details, contract table, enumerator tables, "Defined in" header path, see-also links.
- [ ] `docgen reference` writes `docs/reference/`; `docgen check` regenerates in memory and fails with a diff summary if the committed output is stale.
- [ ] Idempotence + determinism tests (two runs byte-identical; independent of directory iteration order).
- [ ] Golden-output tests from fixtures; unit test for the stale-detection path.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** `docgen reference` produces the reference pages for the fixtures byte-identically across runs, and `docgen check` detects a stale page.
