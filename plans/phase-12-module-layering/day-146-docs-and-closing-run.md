# Phase 12 / Day 146 — Docs, the Measured Graph, Closing Full Run & Wrap-up

**Phase:** [Phase 12 — Module Layering](../phase-12-module-layering.md)
**Depends on:** Days 140-145.

Make the documents describe the new structure, run everything, review the whole phase, close it.

## Tasks

- [ ] **`ARCHITECTURE.md`:** section 2 replaced by the measured graph after the phase (from `module_deps_check`'s output): no cycle, the two RHI entries left for
      Phase 13 Day 162; section 3 marks each item done with the day; section 1 names every library.
- [ ] **`PLAN.md`:** the architecture diagram and repository layout match (`engine/platform/` holds windows and input; the foundation list no longer claims them);
      assumption #11's interface notes point at `engine/platform/`; roadmap row; progress-log entry.
- [ ] **Guides:** `getting-started.md`, `building-and-testing.md`, `platform-os.md`, `platform-windowing-and-input.md`, `foundation-logging-and-assert.md`,
      `foundation-debugging.md`, `porting-debugging.md` read end to end and corrected for moved paths, the hooks and the libraries; every tagged sample compiles and
      runs; `docgen lint` zero; reference regenerated and committed.
- [ ] **Phase 13 plan check:** every path, library name and "Depends on" in `plans/phase-13-windows-dx12*` matches what this phase built (the Windows days build
      `foundation_base` first, then `foundation_io`, `foundation_debug`, `foundation_settings`, `platform`); correct any that drifted.
- [ ] **Known-issues sweep:** the allow-listed RHI edges filed against Phase 13 Day 162; anything a day left; entries this phase fixed removed.
- [ ] **Closing full run (CLAUDE.md, required for a phase's last day), on the Linux machine:** `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, both
      demos 1000 frames with `--validation` on Wayland and X11; record the final `ran X of Y tests (phases ...)` line here; nothing left out.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 140 to the
      working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] Update `plans/phase-12-module-layering.md` (status complete, days done), `STATUS.md`; `CURRENT_PHASE` stays 12 until Phase 13 starts (its day files exist);
      commit and push.

**Done:** the code, `ARCHITECTURE.md`, `PLAN.md` and the guides describe the same layered structure, `module_deps_check` passes with only the two Phase 13 entries,
the closing line `ran X of Y tests (phases: all; flavors: all)` is recorded with every test passing or a filed known issue, and Phase 12 is marked complete.
