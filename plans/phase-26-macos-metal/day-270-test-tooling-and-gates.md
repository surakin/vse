# Phase 26 / Day 270 — Test Tooling, Gates and Docs on macOS

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 269.

From today every macOS day is scoped and gated like a Linux day: `scripts/test-phase.sh` runs on the Mac, knows the side phase, and every gate and the
docs build pass here.

## Tasks

- [ ] **`scripts/test-phase.sh` on macOS:** default build directory `build-macos` on Darwin; run it under `/bin/bash` 3.2 and fix what fails (associative
      arrays, `mapfile`, `${x,,}`, GNU `sed -i`, `readlink -f`, `date +%N`, `nproc`); a bash newer than 3.2 is required only if a feature has no 3.2 form
      (then `BUILD.md` says so). The flavor list on macOS is `plain,asan,tsan`; `--flavor all` runs the three.
- [ ] **Side phases:** a `SIDE_PHASES` file (one phase number per line, `26` today) read by the script; `--affected` adds every side phase to the selection
      on any machine that builds its tests, and the summary line names it (`phases: 18, 26 (side), ...`). `test_phase_labels` accepts labels of a side
      phase above `CURRENT_PHASE` and fails on a label above it that is not a side phase. Tests for the script (`tests/test_phase_script`) cover both.
- [ ] **Gates on macOS:** build and run `module_deps_check`, `platform_neutral_check`, `std_usage_check`, `file_access_check`, `config_access_check`,
      `signal_safety_check`, `raw_new_check`, `rhi_boundary_check`, `settings_fixtures_check`, `ui_widget_style_check`, `test_phase_labels`; fix every
      path, `find`/`sed` or case assumption that breaks on macOS.
- [ ] **The rules learn macOS** (`platform_neutral_check`): AppKit, Foundation, QuartzCore, CoreGraphics, IOKit, Metal, Mach, `dispatch`, `copyfile`,
      `sysctl` and `objc` headers only under `*/macos/` (and `engine/rhi/metal/` from Day 284), `.mm` files only there too; planted-violation fixtures for
      a framework header in a neutral file and a `.mm` outside a macOS directory. `std_usage_check` scans `.mm` files and treats framework headers as OS
      headers (not standard); a planted `<vector>` in a `.mm` still fails.
- [ ] **Cross-syntax gates:** the `*_cross_syntax_macos` checks (types, io, settings, debug, rhi) keep their meaning now that a real macOS build exists:
      keep them (they cover arm64 syntax, which this Mac does not build natively) and say so in their comments.
- [ ] **`docgen` on macOS:** build the tool, `cmake --build build-macos --target docs` produces the same `docs/reference/` bytes as Linux (compare, fix any
      ordering or path difference), and `ctest -L docs` passes, including the guide samples (`tools/docgen/src/samples.cpp`'s platform branch).
- [ ] **Docs:** `docs/guide/building-and-testing.md` ("Test phases and scoped runs": side phases; "Building and testing on macOS": the script, the flavors,
      no LeakSanitizer).
- [ ] **Runs:** `scripts/test-phase.sh --affected` on macOS in all three flavors (quote the lines); the Linux and Windows runs of the shared script and gate
      changes filed.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the script's portability fixes changing nothing on Linux and Git Bash, the label check
      for side phases (a typo cannot hide a test).
- [ ] Update `STATUS.md` (the macOS commands), `KNOWN-ISSUES.md`; commit and push.

**Done:** `scripts/test-phase.sh --affected --flavor all` runs on the Mac and quotes `ran X of Y tests (phases ...)` with phase 26 named as a side phase,
every gate passes on macOS with the new macOS rules and their planted fixtures, the reference built on macOS is byte-identical to Linux's, and
`ctest -L docs` passes on macOS.
