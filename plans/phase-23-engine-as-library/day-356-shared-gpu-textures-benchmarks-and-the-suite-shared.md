# Phase 23 / Day 356 — Shared GPU Textures, Benchmarks & the Whole Suite Against the Shared Build

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 352-355.

The second embedding fallback, the cost of the shared build, and every existing suite run against it.

## Tasks

- [x] Shared GPU textures for hosts that composite themselves, as the spec decided on Day 342 (built where it is cheap, or recorded as not built with the reason): Vulkan external memory (`VK_KHR_external_memory_fd`), D3D12 shared handle, `IOSurface`; `engine_caps` bit and a C-host test that imports the texture into its own Vulkan device on Linux (or the filed reason).
- [x] Benchmarks (hidden): library size and start-up time, the static against the shared build for `renderer_bench`, `ecs_bench` and the Phase 22 asset benchmarks (the static default must not get slower; the shared build's cost is recorded), per-call cost of the C API (a frame call, an entity set, a resource load) against the C++ call.
- [x] Run the **existing suites against the shared build** (the mode argument of Day 343): `scripts/test-phase.sh --all --flavor all` in the shared configuration on Linux; every failure that is not a test of the shared build itself is a bug of the shared build (a duplicated singleton, a hidden symbol a test needed, a header-defined static); fix or record each.
- [x] The plugin question of the phase file (a game as a loadable library that the host reloads while running): written as a spec section with the decision (the planner's default: not in this phase, the C ABI makes it possible), the reasons and what it would need.
- [x] Measured results into the spec's table.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the whole suite passes in every flavor with the engine built both ways on Linux, the benchmark table is in the spec, and the fallbacks are built or filed.

## Outcome

Release benchmarks, the decisions and the shared-configuration full run are in the spec section "Day 356 results". Shared GPU textures not built (reasons there); plugin question: not in this phase; the suite ran in the shared configuration rather than linked to the hidden-symbol library. Three-machine checks wait for the Day 357 closing runs (`KNOWN-ISSUES.md`). Runs: shared full `ran 8915 of 8915 tests (phases: all; flavors: all; display: included; crash tests: included)`; plain `ran 3372 of 8917`, asan `ran 2684 of 8917`, tsan `ran 2349 of 8917` (all `--affected`).

## Post-implementation code review

`/code-review high`: 3 findings. Fixed: the std_usage_check allowlist listed `<time.h>` for two files (the gate failed as stale); the entries were corrected. Declined: the tutorial program's skip paths do not close the display (the process exits at once); the settle loop in `host_reload.c` is bounded at 2 s and keeps the limit, no regression test is practical for a timing flake.
