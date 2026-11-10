# Phase 26 / Day 295 — Guides, Closing Full Runs on Three Machines & Wrap-up

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 294.

The macOS guide, the documentation pass, the full suites on the Mac, the Linux machine and the Windows PC, the uncapped end-of-phase review, and the
answer to `PLAN.md` assumption #11.

## Tasks

- [ ] **New guide `docs/guide/macos.md`:** prerequisites (with the versions from `BUILD.md`), the preset, the flavors (no LeakSanitizer), the shader
      minting route, the Apple header macro wrapper, where macOS code goes, Objective-C++ and ARC rules, dSYMs and the symbol store, Metal validation
      variables, what is not verified (Apple silicon); linked from `docs/README.md` and `getting-started.md`.
- [ ] **Guide pass** (read each as a new developer, fix what is stale): `getting-started.md`, `building-and-testing.md`, `platform-os.md`, `io.md`,
      `settings.md`, `foundation-debugging.md`, `porting-debugging.md`, `jobs.md`, `platform-windowing-and-input.md`, `rhi.md`, `rhi-shaders.md`,
      `renderer.md`, `text.md`, `ui.md`; tagged samples compile and run on macOS.
- [ ] **Project documents:** `BUILD.md` macOS section marked supported with every version checked; `CLAUDE.md` gets a "macOS is a supported platform"
      section (as the Windows one) and the side-phase rule confirmed or removed by the user; `PLAN.md` (roadmap row, log entry, assumptions #11 and #18);
      `ARCHITECTURE.md` current.
- [ ] **Assumption #11:** write the answer (proposed: every later phase gets a macOS day like its Windows PC day; the closing run of every phase includes the
      Mac) into `PLAN.md` and close its `KNOWN-ISSUES.md` line once the user confirms.
- [ ] **Known-issues sweep:** every Phase 26 deferral filed (Apple silicon behavior, the forced-Managed coverage, HDR on a real EDR display, IME
      composition, argument buffers, eGPU removal on real hardware, LeakSanitizer); every "waiting for the other machine" item from Days 266-294 run and
      removed.
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** macOS `scripts/test-phase.sh --all --flavor all` (plain, asan, tsan) plus
      `ctest -L docs` and the demos, unity on and off, and the arm64 build; the Linux machine `--all --flavor all` and the Windows PC `--all --flavor all`
      (both of `master` at the same commit), both unity modes and both Windows compilers; quote every line here.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 266 to
      the working tree, Phase 26 commits only where the main line interleaves: list the commit range used); verify, fix or decline each finding and
      record the count and outcome below.
- [ ] Update `plans/phase-26-macos-metal.md` (status complete), `PLAN.md`, `STATUS.md`, the memory notes (`claude-memory/`); `SIDE_PHASES` loses `26`;
      `CURRENT_PHASE` unchanged (it belongs to the main line); commit and push.

**Done:** `docs/guide/macos.md` and every touched guide are current, the closing lines of the full runs on all three machines are quoted with phase 26
included, the uncapped review's findings are fixed or declined with reasons in this file, assumption #11 has its recorded answer, and Phase 26 is marked
complete in the phase file, `PLAN.md` and `STATUS.md`.
