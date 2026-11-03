# Phase 2.5 / Day 43 — Lint, Coverage & Build Integration

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Days 40-42.

Turns the convention into an enforced rule.

## Tasks

- [ ] Coverage report: documented / total public symbols per module and overall; `docgen lint --baseline FILE` fails if coverage drops below the committed baseline or any new undocumented public symbol appears.
- [ ] Lint rules, configurable per module in the module map: summary required; `Thread-safety:` required on public types in modules marked thread-aware (jobs, async I/O); `Ownership:` required on functions returning raw pointers or taking ownership; `Fails:`/`Aborts:` required where the body asserts or returns failure (heuristic: declared via label presence on `bool`/pointer returns in modules marked strict).
- [ ] CMake: a `docs` target that builds `docgen` and runs `reference`, `check`, `links`, `samples`, `lint`; ctest entries `docs_check`, `docs_links`, `docs_samples`, `docs_lint`.
- [ ] Make `docgen` timing sane (full run over `engine/` under a few seconds); document how to run it in `docs/guide/building-and-testing.md`.
- [ ] Commit the initial lint baseline (expected to be low until Day 44 retrofits the headers).
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** `ctest` runs the four docs checks, they pass on the current tree, and introducing an undocumented public symbol, a broken link or a non-compiling sample makes the matching check fail.
