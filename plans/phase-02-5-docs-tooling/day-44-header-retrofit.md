# Phase 2.5 / Day 44 — Retrofit Existing Headers & Adopt the Rule

**Phase:** [Phase 2.5 — Documentation Tooling](../phase-02-5-docs-tooling.md)
**Depends on:** Day 43.

Brings Phases 0-2 up to the new convention so the tool starts from a clean, fully documented baseline.

## Tasks

- [ ] Foundation memory, containers, types: convert existing comments to `///`, add missing summaries and contract labels.
- [ ] Foundation strings/Unicode and math.
- [ ] Foundation log/assert and the OS layer (`os.h`, file util, command line, driver probe, window, input, Linux Wayland/X11/xkb headers).
- [ ] Jobs: `job.h`, `scheduler.h`, `counter.h`, `deque.h`, `worker_pool.h`, `topology.h`, `frame_pool.h`, `parallel_for.h`, `async_io.h` — move the existing contract prose (ownership, lifetime, shutdown, capacity) into labeled fields.
- [ ] Fix the module map and lint config so strict modules are strict; raise the baseline to the achieved coverage (target 100% of public symbols).
- [ ] Regenerate and commit `docs/reference/`; fill in the `docs/guide/` pages' API-link placeholders using `[[refs]]`.
- [ ] **Propose** adding the "a day that changes public API is documented before it is done" rule to `CLAUDE.md` and `PLAN.md` and wait for the user's approval before editing them.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** coverage for Phases 0-2 is 100% (or each exception is listed with a reason), all four docs checks are green, and the user has decided on the CLAUDE.md rule.
