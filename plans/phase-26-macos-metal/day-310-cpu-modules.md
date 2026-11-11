# Phase 26 / Day 310 — Text, UI, ECS and Spatial on macOS (the CPU Modules)

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 309.

The modules above the foundation that have no platform code: `engine/text` (with vendored FreeType), `engine/ui` (CPU side, with the header-only
`platform_input`), `engine/ecs`, `engine/spatial`, and any module the main line has added by today at the same layer (check `ARCHITECTURE.md` when the
day starts). They should build unchanged; today proves it and fixes what Apple clang and libc++ find.

## Tasks

- [ ] **Inventory at the start:** list the modules on `master` at layers 10-11 that need no window or GPU (`ARCHITECTURE.md`), and their test
      directories; any module added since the plan was written is included here and recorded.
- [ ] **FreeType** (`third_party/freetype/`) builds on macOS with our CMake (its own flags kept; no `-framework` dependency); the vendored build options
      unchanged; `text/font_freetype.cpp` compiles with Apple clang.
- [ ] **Configure and build** `engine/text`, `engine/ui`, `engine/ecs`, `engine/spatial` and their tests on macOS (`tests/text`, `tests/ui`, `tests/ecs`,
      `tests/spatial`), the CPU-only parts; tests that need a window or a device stay off until Days 311-324 and are listed.
- [ ] **Suites:** all pass in plain, asan and tsan; the ECS parallel scheduler and the spatial sync under TSan are the concurrency checks of the day.
      Record any result that differs from Linux (floating-point in geometry or layout tests: the same inputs must give the same outputs; a difference is a
      bug or an unstated tolerance, not a macOS exception).
- [ ] **Text output determinism:** the glyph atlas and layout tests compare against the same expected values as Linux (FreeType is vendored, so the bytes
      must match); a mismatch is investigated (compiler `-ffp-contract`, FMA on this CPU) and fixed in flags or code.
- [ ] **Benchmarks** that are CPU-only (`ecs_bench`, the spatial benchmarks): run once and recorded beside the other machines' numbers in their guides, no
      targets.
- [ ] **Documentation:** the guides of each module note "runs on macOS" where they list platforms; `docgen lint` zero; reference regenerated if any
      header changed.
- [ ] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: any code change made for libc++ or Apple clang staying correct on Linux and MSVC.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** text, UI (CPU), ECS, spatial and any other window-free module on `master` build and pass their suites on macOS in three flavors and both unity
modes, with byte-identical text outputs to Linux and the benchmark numbers recorded.
