# Phase 23 / Day 356 — Shared GPU Textures, Benchmarks & the Whole Suite Against the Shared Build

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Days 352-355.

The second embedding fallback, the cost of the shared build, and every existing suite run against it.

## Tasks

- [ ] Shared GPU textures for hosts that composite themselves, as the spec decided on Day 342 (built where it is cheap, or recorded as not built with the reason): Vulkan external memory (`VK_KHR_external_memory_fd`), D3D12 shared handle, `IOSurface`; `engine_caps` bit and a C-host test that imports the texture into its own Vulkan device on Linux (or the filed reason).
- [ ] Benchmarks (hidden): library size and start-up time, the static against the shared build for `renderer_bench`, `ecs_bench` and the Phase 22 asset benchmarks (the static default must not get slower; the shared build's cost is recorded), per-call cost of the C API (a frame call, an entity set, a resource load) against the C++ call.
- [ ] Run the **existing suites against the shared build** (the mode argument of Day 343): `scripts/test-phase.sh --all --flavor all` in the shared configuration on Linux; every failure that is not a test of the shared build itself is a bug of the shared build (a duplicated singleton, a hidden symbol a test needed, a header-defined static); fix or record each.
- [ ] The plugin question of the phase file (a game as a loadable library that the host reloads while running): written as a spec section with the decision (the planner's default: not in this phase, the C ABI makes it possible), the reasons and what it would need.
- [ ] Measured results into the spec's table.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the whole suite passes in every flavor with the engine built both ways on Linux, the benchmark table is in the spec, and the fallbacks are built or filed.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
