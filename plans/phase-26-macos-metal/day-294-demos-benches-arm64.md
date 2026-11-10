# Phase 26 / Day 294 — Demos, Benchmarks, Catch-up and the arm64 Compile Check

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 293.

Every demo and benchmark on Metal, the macOS port of whatever the main line added after this phase's layer days ran, and a compile-only arm64
configuration so Apple silicon code paths at least build.

## Tasks

- [ ] **Catch-up inventory:** diff `ARCHITECTURE.md`, `tests/phases.txt` and `game/` against what the Day 267 inventory and Days 278 and 292 covered;
      every module, test or demo the main line added since is built and run on macOS today; anything too large for one day becomes an inserted day
      before this one (renumbering per CLAUDE.md), not a silent gap.
- [ ] **Demos:** `hello_window`, `input_probe`, `rhi_demo`, `renderer_demo`, `ui_demo` and any newer demo on Metal: scripts pass, `--crash-handler`,
      `--crash-test=segv|abort|assert|ensure`, `--gpu`, `--list-gpus`, `--prewarm` behave as on Linux; the demo ctests pass.
- [ ] **Benchmarks:** `rhi_bench`, `renderer_bench`, `ecs_bench` and any newer benchmark run; numbers recorded in their guides.
- [ ] **arm64 compile check:** a preset `macos-clang-arm64` (`CMAKE_OSX_ARCHITECTURES=arm64`, deployment target 13.0) builds every target with no warning
      in unity and non-unity modes; nothing runs (no hardware). `FaultSupported(DivideByZero)` is false there, the `brk` illegal instruction compiles, the
      16 KiB page size is not assumed anywhere (a grep of constants). It runs on the closing day and on each later macOS day that changes macOS code.
- [ ] **Remove the "not yet ported" CMake branch** of Day 268: everything configures on macOS.
- [ ] **Documentation:** each demo's README names macOS; `KNOWN-ISSUES.md` Apple-silicon line names the compile check and what it does not cover;
      `docgen lint` zero.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` with GPU and display tests (quote); the arm64 build line.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: anything changed for arm64 that the x86-64 build no longer tests.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every demo and benchmark on `master` runs on macOS through Metal, the main line's newer modules are ported or have their own inserted day, the
arm64 configuration builds clean in both unity modes, and no CMake branch says "not ported" on macOS.
