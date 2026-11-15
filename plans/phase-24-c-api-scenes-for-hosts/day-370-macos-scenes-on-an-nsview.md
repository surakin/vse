# Phase 24 / Day 370 — macOS: Scenes on an `NSView` (Metal)

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 358-368 built on Linux; Phases 21, 23 Day 355. **Done on the Mac.**

The scene features on macOS with Metal.

## Tasks

- [ ] Build and run every test of Days 359-368 on `macos-clang` (plain; sanitizers as the memory notes allow) and the arm64 compile check; fix what differs.
- [ ] Metal validation stays on in the tests (CLAUDE.md); goldens under the tolerance of Day 358; the Metal shaders for the new pipelines (labels, per-slot override, skinned) are minted on the Mac (`tools/metal_mint`).
- [ ] Host textures on Metal (shared storage, `replaceRegion`), the reset path, the Cocoa C host (`host_cocoa.m`) forwarding input into the scene.
- [ ] Record in `KNOWN-ISSUES.md` what could not be done.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Mac; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the phase's scene tests pass on the Mac with Metal validation on, or each failure is recorded with its observed behavior.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
