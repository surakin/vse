# Phase 26 / Day 328 — Guides, Closing Full Runs on Three Machines & Wrap-up

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 327.

The macOS guide, the documentation pass, the full suites on the Mac, the Linux machine and the Windows PC, the uncapped end-of-phase review, and the
answer to `PLAN.md` assumption #11.

## Tasks

- [x] **New guide `docs/guide/macos.md`:** prerequisites (with the versions from `BUILD.md`), the preset, the flavors (no LeakSanitizer), the shader
      minting route, the Apple header macro wrapper, where macOS code goes, Objective-C++ and ARC rules, dSYMs and the symbol store, Metal validation
      variables, what is not verified (Apple silicon); linked from `docs/README.md` and `getting-started.md`.
- [x] **Guide pass** (11 pages corrected from a read of all 14; the samples compile and run on macOS: `docs_samples`, Day 327) (read each as a new developer, fix what is stale): `getting-started.md`, `building-and-testing.md`, `platform-os.md`, `io.md`,
      `settings.md`, `foundation-debugging.md`, `porting-debugging.md`, `jobs.md`, `platform-windowing-and-input.md`, `rhi.md`, `rhi-shaders.md`,
      `renderer.md`, `text.md`, `ui.md`; tagged samples compile and run on macOS.
- [x] **Project documents:** (`BUILD.md`, `CLAUDE.md`, `PLAN.md`, `ARCHITECTURE.md` unchanged: no module moved; the side-phase rule is left as the user confirmed it on 2026-11-11, the question of keeping it after Phase 26 is pending) `BUILD.md` macOS section marked supported with every version checked; `CLAUDE.md` gets a "macOS is a supported platform"
      section (as the Windows one) and the side-phase rule confirmed or removed by the user; `PLAN.md` (roadmap row, log entry, assumptions #11 and #18);
      `ARCHITECTURE.md` current.
- [ ] **Assumption #11:** (proposed in `PLAN.md`; **pending the user's confirmation**, `KNOWN-ISSUES.md` keeps the line) write the answer (proposed: every later phase gets a macOS day like its Windows PC day; the closing run of every phase includes the
      Mac) into `PLAN.md` and close its `KNOWN-ISSUES.md` line once the user confirms.
- [x] **Known-issues sweep:** (the deferrals filed; the "waiting for the other machine" items stay, with the closing runs, for the user to run) every Phase 26 deferral filed (Apple silicon behavior, the forced-Managed coverage, HDR on a real EDR display, IME
      composition, argument buffers, eGPU removal on real hardware, LeakSanitizer); every "waiting for the other machine" item from Days 298-326 run and
      removed.
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** **left to the user, by their decision (2026-11-13): "Skip the tests. Finish this phase. I'll run them later."** The run done is below. macOS `scripts/test-phase.sh --all --flavor all` (plain, asan, tsan) plus
      `ctest -L docs` and the demos, unity on and off, and the arm64 build; the Linux machine `--all --flavor all` and the Windows PC `--all --flavor all`
      (both of `master` at the same commit), both unity modes and both Windows compilers; quote every line here.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 298 to
      the working tree, Phase 26 commits only where the main line interleaves: list the commit range used); verify, fix or decline each finding and
      record the count and outcome below.
- [x] Update `plans/phase-26-macos-metal.md` (status complete) (`SIDE_PHASES` keeps `26`: see the amendment), `PLAN.md`, `STATUS.md`, the memory notes (`claude-memory/`); `SIDE_PHASES` loses `26`;
      `CURRENT_PHASE` unchanged (it belongs to the main line); commit and push.

**Done:** `docs/guide/macos.md` and every touched guide are current, the closing lines of the full runs on all three machines are quoted with phase 26
included, the uncapped review's findings are fixed or declined with reasons in this file, assumption #11 has its recorded answer, and Phase 26 is marked
complete in the phase file, `PLAN.md` and `STATUS.md`.

## Plan amendments (made during the day)

- **The closing runs are the user's, later** (their decision, 2026-11-13: "Skip the tests. Finish this phase. I'll run them later."): the phase is
  marked complete without the final full runs; what is missing is listed below and in `KNOWN-ISSUES.md`.
- **`SIDE_PHASES` keeps `26`:** `test_phase_labels` fails on a phase above `CURRENT_PHASE` (20) that is not a side phase, so removing it now would break
  the gate; it goes when the main line passes Phase 26, or when the check learns that a finished side phase is allowed. It also keeps phase 26's tests in
  every `--affected` run on the Mac, which the macOS days of later phases want.

## Implementation notes

- **`docs/guide/macos.md`** (new, modelled on `windows.md`): at a glance, prerequisites, building (both presets, flavors, unity), where macOS code goes
  (directories, Objective-C++ and ARC, autorelease pools, Apple's macros), tests, demos, Metal, dumps and dSYMs, input, where things live, common mistakes,
  known gaps; linked from `docs/guide/README.md`, `getting-started.md` (with an "On macOS" build block) and `docs/README.md`.
- **The guide pass:** a read of the fourteen pages found stale statements in eleven (`building-and-testing`, `getting-started`, `platform-os`,
  `foundation-debugging`, `porting-debugging`, `jobs`, `platform-windowing-and-input`, `rhi`, `rhi-shaders` (a new "Metal libraries" section), `renderer`;
  `io`, `settings`, `text` and `ui` were current), each corrected; two statements stale from before the phase went too (a "not yet on any backend" list
  that named built features, a future-tense Day 161 note).
- **Project documents:** `BUILD.md` (macOS supported, every version checked again on this machine), `CLAUDE.md` ("macOS is a supported platform", three
  machines, the constraint's status), `PLAN.md` (the roadmap row, assumptions #11 (proposed) and #18, the log).
- **Known issues:** the Phase 26 deferrals filed (HDR on an EDR display, IME composition, argument buffers, an eGPU's removal; Apple silicon, the forced
  Managed coverage and LeakSanitizer were already); the review's declined performance items.

## Runs

- **The Mac, before the end-of-phase review's fixes:** `ran 7414 of 7414 tests (phases: all; flavors: all; display: included; crash tests: included) in
  1782 s`. Failed only the known: the stale DXIL test (three flavors; the PC re-minted the DXIL the same day, `0c8a1600`), `Throughput scales with participant count` and `A child that
  writes without pause is still killed at the timeout [tsan]` (both flaky under load, `KNOWN-ISSUES.md`). **The window tests skipped: the screen had
  locked** (289 skipped in all). `ctest -L docs` 7 of 7 (Day 327). The arm64 trees: both unity modes clean (Day 327).
- **After the review's fixes:** built in both unity modes (x86-64); the Metal suites under full validation (44 test cases), the archive tests (4), the UI
  suites (382 of 382) and the reflection tests pass. Not run: the full suite again, with the screen unlocked.
- **Not run (the user's, later):** the Mac's closing `--all --flavor all` after the fixes with the screen unlocked, and the demos by hand again; the Linux
  machine's and the Windows PC's `--all --flavor all` of `master` at one commit, both unity modes and both Windows compilers (the PC ran Days
  315-326 on 2026-11-13, `0c8a1600`); the arm64 build of the fixes.

## Post-implementation code review (end of phase, uncapped)

`/code-review high --max-findings all` over the Phase 26 commits (`d0ef87eb` to `84464f12`, 36 commits, the main line's interleaved commits left out)
and the working tree: **17 findings, 9 fixed, 8 declined.**

1. **A Managed `CpuToGpu` buffer the GPU writes read back stale** (no `synchronizeResource:`). Fixed: a CPU-written buffer the GPU may write (copy
   destination, storage) is Shared; test under forced Managed storage (a GPU copy reads back).
2. **Compute and graphics shared one bound state**, so a pass's pipeline and groups replaced the compute ones (Vulkan and Direct3D 12 keep a bind point
   each). Fixed: a bind point per kind, groups and push constants to the kind bound last, as on Vulkan; test.
3. **The index buffer was held unretained**, and a `WaitIdle` in the frame collects what was destroyed. Fixed: held (the contract still forbids using a
   destroyed resource; Vulkan's `WaitIdle` collects the same).
4. **16-bit index offsets not a multiple of 4.** Declined: measured on Day 325, Metal's validation accepts it and the right indices are drawn (a test).
5. **The user defaults watcher does not synchronize the cache.** Declined: the test of another process's `defaults write` passes; cfprefsd reports it.
6. **Cmd+click on a selected list row could start a drag.** Fixed: the press test uses the shortcut modifier, as the click does.
7. **Cmd+Y does not redo.** Declined: macOS redo is Cmd+Shift+Z, which works; Ctrl+Y is the Windows convention.
8. **Timeline listeners for values never reached pile up.** Declined: no listener is registered once the device is lost, and any other is reached.
9. **The pipeline cache keeps every pipeline's description for the device's life.** Declined: one per distinct description, ended by a reset.
10. **`ResolveGroup` takes the device lock per dirty bind.** Declined for now: measured per draw unchanged (`rhi_bench`); filed.
11. **The pipeline cache's byte and hash helpers repeat Direct3D 12's.** Declined for now: sharing them touches the D3D12 file, which only the PC builds;
    filed.
12. **The managed preferences are re-read four times a second with a listener.** Declined for now: a cost, not a fault; filed.
13. **A drag was announced before its session existed.** Fixed: announced after a session is made.
14. **A reopened window could miss its pointer enter.** Fixed: `Open` starts with the pointer outside.
15. **The swapchain's formats leave out `RGBA16_Float`.** Declined: `formats` lists the sRGB-nonlinear formats by contract, and scRGB is offered through
    `hdr_scrgb`, as on Direct3D 12.
16. **Overlapping Metal slot ranges of arrays passed the reflection check.** Fixed: ranges are compared; test.
17. **The docs target lost its explicit `rhi_vulkan` dependency.** Fixed: named again on Linux and Windows.
