# Phase 24 / Day 358 — Design Spec, Spikes & Phase Setup

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Phase 23 closed (Day 357), Phases 5, 8, 14, 17, 22.

Settles the open questions of the phase file with the planner's defaults and five spikes, writes the spec, and starts the phase.

## Tasks

- [ ] Write `docs/superpowers/specs/2026-11-NN-c-api-scenes-design.md` with the **decisions** and their reasons: (1) coordinate conventions at the boundary (the engine's, as the importer produces it: Phase 22 Day 336 "no flip"; cameras look along -Z, as `scene::Camera` documents; the header states it once); (2) the surface names the camera (`surface_set_world(surface, world, camera_entity)`), several surfaces may show one world; (3) material overrides per slot instead of material creation; (4) the fixed order of the frame's systems (animation, transform, render sync, render) and when a host may read poses; (5) joint attachment: yes or later, decided by the spike; (6) labels: a billboard plate drawn through the glyph atlas of Phase 14 (or the spike's alternative); (7) the ABI version rule for the phase (Phase 23 has not been released, so the additions stay in the same major and minor unless the spec finds a reason; the snapshot and `abi_check` still record every change); (8) golden-image tolerances across GPUs; (9) which parts of the renderer a surface owns (one `renderer::Renderer`, `RenderScene` and `SceneLink` per surface, or per world and device) and how a world's GPU objects follow several devices.
- [ ] **Spike 1, a renderer on a surface's device:** in a throwaway test build a `renderer::Renderer` on the device and swapchain a surface already has (Day 352), mirror a world with `scene::RenderSync` into a `RenderScene`, draw the G-buffer and lighting passes into the swapchain image, and read the frame back; record what the renderer needs from its host (`jobs::Scheduler`, `DeviceResources`, settings) and what that costs in `engine_c`'s dependencies (`ARCHITECTURE.md`: `engine_c` gains `renderer`, `scene`, `animation`, `ui`/`text` edges; the module layering stays acyclic).
- [ ] **Spike 2, GPU objects of content on a surface's device:** how a mesh or texture of the resource host (Phase 22 GPU-backed resources, `GpuMesh`, `GpuTexture`, `DeviceResources`) becomes resident on the surface's device when the resource host and the surface have different devices (Day 352 gave each surface its own); the options (the surface owns a device-resident cache keyed by resource id and generation; or one shared device for surfaces on the same GPU) with a measurement of upload cost and of what a reset does to each.
- [ ] **Spike 3, live texture upload:** a texture updated from a second thread every frame at 1280x720 and at 320x180: staging buffer reuse, latest-wins slot, upload time per frame, and what the sync validation says; the budget numbers the spec states.
- [ ] **Spike 4, a world-space label:** the same text drawn as a camera-facing quad textured from the glyph atlas (constant pixel size and constant metre size), compared with the text renderer's 2D path projected from a world point; pick one and record the limits (depth test, plates, many labels, DPI).
- [ ] **Spike 5, golden images across GPUs:** render a lit, textured, skinned test frame on the Linux GPU and on lavapipe or the null path where available; record how much pixels differ, and fix the comparison rule (per-channel tolerance and a count of outliers) the phase's goldens use.
- [ ] Set `CURRENT_PHASE` to 24, add `tests/phases.txt` lines for the spikes' tests, and list every spike decision in the phase file's "Notes" with the day that builds it.
- [ ] Check that the day files 359-372 still fit the decisions; edit them (and `PLAN.md`) before Day 359 if a spike changed something; the spec lists what moved.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec is written and committed, all five spikes have recorded numbers or findings, `CURRENT_PHASE` is 24, and the day files are consistent with the decisions.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
