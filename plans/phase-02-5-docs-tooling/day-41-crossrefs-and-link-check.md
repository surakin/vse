# Phase 2.5 / Day 41 — Cross-References & Link Checker

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 40.

Makes the docs refuse to rot: broken references fail the build.

## Tasks

- [ ] Resolve `[[Symbol]]` references in doc comments and in `docs/**/*.md` against the symbol index (qualified, partially qualified, overload hints); rewrite to Markdown links in generated output.
- [ ] Errors for unresolved or ambiguous references with the candidate list.
- [ ] Markdown link checker for `docs/**` and the repo's top-level `.md` files: relative file links, `#anchor` links (including generated heading anchors), image paths; external `http(s)` links are listed but not fetched.
- [ ] Orphan-page report (pages not reachable from `docs/README.md`).
- [ ] `docgen links` command; exit non-zero on any error.
- [ ] Tests: resolved/unresolved/ambiguous refs, anchor generation matching the emitter, orphan detection, and a run over the real `docs/` tree.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** `docgen links` is clean on the real docs tree and each class of broken reference has a failing test proving it is caught.
