# Phase 21 / Day 311 — Demos, Benchmarks, Catch-up and the arm64 Compile Check

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 310.

Every demo and benchmark on Metal, the macOS port of whatever the main line added after this phase's layer days ran, and a compile-only arm64
configuration so Apple silicon code paths at least build.

## Tasks

- [x] **Catch-up inventory:** diff `ARCHITECTURE.md`, `tests/phases.txt` and `game/` against what the Day 283 inventory and Days 294 and 308 covered;
      every module, test or demo the main line added since is built and run on macOS today; anything too large for one day becomes an inserted day
      before this one (renumbering per CLAUDE.md), not a silent gap.
- [x] **Demos:** `hello_window`, `input_probe`, `rhi_demo`, `renderer_demo`, `ui_demo` and any newer demo on Metal: scripts pass, `--crash-handler`,
      `--crash-test=segv|abort|assert|ensure`, `--gpu`, `--list-gpus`, `--prewarm` behave as on Linux; the demo ctests pass.
- [x] **Benchmarks:** `rhi_bench`, `renderer_bench`, `ecs_bench` and any newer benchmark run; numbers recorded in their guides.
- [x] **arm64 compile check:** a preset `macos-clang-arm64` (`CMAKE_OSX_ARCHITECTURES=arm64`, deployment target 13.0) builds every target with no warning
      in unity and non-unity modes; nothing runs (no hardware). `FaultSupported(DivideByZero)` is false there, the `brk` illegal instruction compiles, the
      16 KiB page size is not assumed anywhere (a grep of constants). It runs on the closing day and on each later macOS day that changes macOS code.
- [x] **`docs_samples` on macOS** (moved from Day 286 by way of Day 293): the guide samples compile and run with Apple clang, linking the macOS
      libraries (`tools/docgen/CMakeLists.txt` gets a Darwin branch for the frameworks in place of pkg-config's Wayland and X11); `ctest -L docs`
      passes on macOS.
- [x] **Remove the "not yet ported" CMake branch** of Day 284: everything configures on macOS.
- [x] **Documentation:** each demo's README names macOS; `KNOWN-ISSUES.md` Apple-silicon line names the compile check and what it does not cover;
      `docgen lint` zero.
- [x] **Runs:** `ran 7034 of 7414 tests (phases: all; flavors: all; display: included; crash tests: excluded) in 3489 s` (only the known stale DXIL test, in three flavors, and the known flaky `Throughput scales with participant count`, 3 of 3 on rerun; after the review's fixes, which change only CMake and documentation, the x86-64 trees were rebuilt in both unity modes and `ctest -L docs` passed 7 of 7); arm64: both unity modes clean (`build-macos-arm64`, 626 build steps; `build-macos-arm64-nounity`, 2140), no warning, every program a Mach-O arm64 executable, `ctest` there finds no tests. Originally: `scripts/test-phase.sh --affected --flavor all` with GPU and display tests (quote); the arm64 build line.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: anything changed for arm64 that the x86-64 build no longer tests.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** every demo and benchmark on `master` runs on macOS through Metal, the main line's newer modules are ported or have their own inserted day, the
arm64 configuration builds clean in both unity modes, and no CMake branch says "not ported" on macOS.

## Plan amendments (made during the day)

- **The arm64 preset's deployment target is 13.3, not 13.0:** the same as the x86-64 preset (`macos-clang`), so both builds see the same API
  availability; 13.3 is what the port targets (Day 283's spec), and a 13.0 target would only test availability the shipping build does not have.
- **`ENGINE_COMPILE_ONLY`** carries the "nothing runs" rule: no Catch2 discovery, no shader minting (the committed outputs), and testing not
  enabled (the tests are declared, so every script configures, but no CTestTestfile is written).

## Implementation notes

- **Catch-up inventory:** the root `CMakeLists.txt`'s macOS branch now has every directory of the Linux build (the gates through their
  `foreach`); the only `NOT APPLE` guards left are the Vulkan-only demo tests and `tools/metal_mint`'s inverse. The main line added nothing since
  Phase 20 (Day 281), which Day 310 ported; no inserted day was needed.
- **Demos on Metal:** `rhi_demo`, `renderer_demo` and `ui_demo` write their screenshots on Metal with `--gpu 0`; `--list-gpus` lists the Iris Plus
  645 (0 MiB device-local: unified memory); `--prewarm` works; `--crash-handler=on --crash-test=segv|abort|assert|ensure` end with exit 1, 5, 5 and 0,
  each with its report (macOS's Breakpad exits with the exception type, Day 298). `hello_window`, `input_probe`, `spatial_demo`, `storage_demo`
  and `net_demo` run under their ctests. The demos' ctests pass in the run below.
- **Benchmarks (Release):** `rhi_bench` and `renderer_bench` (Day 309's numbers, now in `rhi.md` and `renderer.md`); `ecs_bench` (iteration at 100k
  21.5x, 4 workers at 80 percent, every pass condition holds; `ecs.md`); the network benchmarks (`net_tests_sockets "[net-bench]"`: TCP echo p50
  28 us, UDP 47 us, 1,000 simulated connections 47 us per connection-second, the simulated stream 965 MiB/s; the networking spec, section 5).
- **arm64:** preset `macos-clang-arm64` (`build-macos-arm64`, `CMAKE_OSX_ARCHITECTURES=arm64`, `ENGINE_COMPILE_ONLY`); a native Apple silicon
  configure is also accepted (and runs its tests). `FaultSupported(DivideByZero)` is false on arm64 and the illegal instruction is `udf #0`
  (both already in `fault_injection_posix.cpp`); the bus-error page uses `getpagesize()`; the one 4096 in macOS code is a tolerance above the
  stack pointer in the crash hook, not a page size.
- **`docs_samples` on macOS:** a Darwin branch in `tools/docgen/CMakeLists.txt` (the SDK through `xcrun`, the deployment target, the
  architecture, the macOS definitions, the Metal backend, the frameworks); 107 samples compile, 96 run. One sample assumed every platform
  registers Vulkan (it crashed on macOS) and now checks for a GPU backend, the same output everywhere.
- **The "not yet ported" branch** of the root `CMakeLists.txt` and its message are gone, with the stale null-backend line.
- **Docs:** the demos' READMEs (macOS sections: build, Metal validation, the settings, dumps and cache paths), `building-and-testing.md` (the
  arm64 preset), `rhi.md` (backends per platform, the sample), `renderer.md`, `ecs.md`, the networking spec; `KNOWN-ISSUES.md`'s Apple silicon
  line names the compile check and what it does not cover.

## Post-implementation code review

`/code-review high` on the day's diff: 8 findings, all fixed.

1. **The samples were compiled without `-arch`**, which would not link in the arm64 tree. Fixed: the build's architectures are passed.
2. **A native Apple silicon configure without `CMAKE_OSX_ARCHITECTURES` stopped**, and the compile-only line printed when it was not. Fixed.
3. **Compile-only still registered every plain `add_test`**, so `ctest` there would run arm64 programs. Fixed: testing is not enabled in
   compile-only (checked: `ctest` in `build-macos-arm64` finds no test).
4. **`renderer_demo`'s README said the backend switches at run time** (it is chosen at start, the user's decision of 2026-11-08). Fixed.
5. **Two stale status lines in the macOS branch.** Fixed.
6. **The deployment target differed from the plan's 13.0 without a plan edit.** Fixed: amended above, with the reason.
7. **Day file and `STATUS.md` not updated.** Done in this commit.
8. **`DOCGEN_RHI_BACKEND` duplicated what `rhi_backends` links.** Fixed: removed.
