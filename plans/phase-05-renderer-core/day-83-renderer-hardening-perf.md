# Phase 5 / Day 83 — Hardening, Sanitizers & Performance

**Phase:** [Phase 5 — Renderer Core](../phase-05-renderer-core.md)
**Depends on:** Day 82.

Make the renderer robust and know what it costs.

## Tasks

- [x] Sanitizers: `renderer_tests_asan` green with GPU tests, `renderer_tests_tsan` green for the null-device parallel recording and graph tests, leak counters (renderer-level and `GetLeakCounts`) at zero after every test; suppressions only with a documented reason.
- [x] Failure injection: device loss at each stage of a frame (before the G-buffer, mid-lighting, at present), an allocation failure creating a transient, a material with a missing texture, a pipeline that fails to build - each ends with a logged error, a defined status and a destroyable renderer.
- [x] Review every `ENGINE_ASSERT` and log site in `engine/renderer`, classify each (programming error vs environment) as for the RHI, and document with `Aborts:`/`Fails:`.
- [x] Benchmarks (`game/renderer_bench`, recorded in the day file, never asserted): graph build+compile time vs pass count, CPU frame time for 1k/10k/50k objects (single thread vs jobs), draws and bind changes after sorting, light volume cost per light, transient pool hit rate; compare per-draw cost with the Phase 4 `rhi_bench` figure (0.5-0.6 us) and explain the difference.
- [x] Profile the 10k-object frame with `perf`, record the top hotspots as notes for later phases, and fix only defects.
- [x] Run the whole suite plain/ASan/TSan and record the renderer test set's timing against a documented budget.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The failure-behavior table and performance notes are in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** The renderer passes all sanitizer and failure-injection runs, its costs are measured and explained, and nothing leaks over long runs.

## Result

**Sanitizers.** Whole suite: 1826 tests pass in the plain, ASan and TSan flavors (`ctest -j16`, 65 s wall). `renderer_tests_asan`
runs the GPU tests; `renderer_tests_tsan` runs everything but `[gpu]`. Leaks: the null device counts live objects and every fault
test checks it is empty after teardown; the real-device tests check `VulkanDevice::GetLeakCounts()` is zero. The only LeakCounts
suppressions are the existing ones in `tests/rhi/lsan.supp`, each with its reason (the NVIDIA driver and libdbus/GLX/X11 process-lifetime
allocations); none was added.

**Failure injection** (`tests/renderer/renderer_fault_test.cpp`, null device; `tests/rhi/vulkan_renderer_demo_test.cpp`, real device):
device lost between frames, in the frame callback, in the first pass and in the last pass (`DeviceLost`, later frames stay lost,
nothing left alive); a sweep of the device lost at frames 3-8 through the full deferred pipeline; a transient texture the device refuses
(`Failed`, names it, pool whole); a missing texture directory (procedural fallback, `Ok`); a mesh whose layout the material cannot read
(object named and left out, `Ok`); a device that cannot build the G-buffer pipelines (every object left out, `Ok`); a device that cannot build
the lighting pipeline (frame setup refuses cleanly); on the real device, a sweep of injected losses at frames 1-10 with validation and sync validation
silent and leak counts zero, and 300 frames with the pool at five textures, none destroyed. **Defect found and fixed:** the loss sweep
crashed in `demo::CreateSampledTexture` (`game/common/textures.cpp`), which dereferenced the null command list `CreateCommandList` returns on a
lost device; it now fails the upload. The test fails without the fix (SIGSEGV).

**Assert and log classification** (all 118 sites in `engine/renderer`): every `ENGINE_ASSERT` (about 90) is a programming error
(null or out-of-range argument, stale id, call out of order); every `RenderLog` site (about 60) is an environment or input failure
(device out of memory or lost, a full list, a shader that does not match the conventions) and returns a value without aborting. No assert
guards an environment condition. Public functions that assert but lacked an `Aborts:` line got one: the six mesh builders, `StripToList`,
`ListToStrip`, `CreateGBufferTemplates`, `CreateGBufferMaterial`, `FitCameraDepthRange` (its `Fails: never` was wrong), the `GBufferPass`,
`GBufferDebugView`, `LightingPass` and `TonemapPass` constructors, `LightScreenRect`, `MaterialSystem::CreateTemplate`;
`RenderGraph::Execute` now says a null list from a lost device is the caller's to check. The guide has the failure-behavior table.

**Benchmarks** (`game/renderer_bench`, Release, RTX 3060, Ryzen 9 7950X3D, 1280x720 offscreen, 300 point lights, device idled outside the timer;
two runs within a few percent; Debug is 5-7x slower, so never tune on it):

| Measurement | Result |
|---|---|
| Graph build + compile, chain of 10 / 100 / 1000 passes | 0.6 / 5.4 / 54 us (0.05 us per pass) |
| Frame, 1000 / 10000 / 50000 objects, single thread | 0.11 / 0.78 / 3.9 ms (0.08 us per object) |
| Same with a job scheduler | 0.15 / 0.80 / 3.9 ms: no gain |
| Draws after sorting (10000 objects, 9312 visible) | 16 draw calls, 3 pipeline binds, 9 material binds (582 objects per draw) |
| Point lights 100 / 1000 / 4000 / 16000 (1000 objects) | +0.01 / +0.15 / +0.8 / +3.1 ms, 0.1-0.2 us CPU per light |
| Transient pool, 100 frames | 5 created, 520 reused, 99% hit rate, none destroyed |

*Against the Phase 4 per-draw figure (0.5-0.6 us):* different quantity. `rhi_bench` records one draw per cube with its own data (a transient
bind group, pipeline, draw): 0.54 us each. The renderer instances objects that share a mesh and material, so 10000 objects are 16 draws;
what it pays per object (0.08 us) is culling, the sort, and writing the object's matrices. Without sharing it would approach one
`rhi_bench` draw per object. The job scheduler does not help because the cost is scene preparation on one thread and the three passes
depend on each other; recording was never the bottleneck here. GPU time per light volume was not measured (no timestamp queries,
already in KNOWN-ISSUES).

**Profile** (`perf record -e cpu-clock:u`, flat, 10000-object frame, Release): draw-sort comparator 18%, `FrameResources::WriteObjects` 8%,
`GBufferPass::Prepare` itself 6%, `MaterialSystem::ReadsVertexInput` 5.7% and `GetPipeline` 4.8% (looked up per object), `Frustum::Visible`
5%, `NormalMatrix` 3%; nothing else above 2%. No defect; notes for later phases: cache the pipeline per (mesh, material) pair, sort by a
precomputed key, compute the normal matrix once per transform change. Filed in KNOWN-ISSUES (the per-object `GetPipeline` row).

**Suite timing against a budget.** Budgets (so a regression is noticed; not asserted): the plain renderer test binary under 15 s, the ASan
flavor under 90 s, the TSan flavor under 15 s. Measured alone: plain 5.7 s (147 tests), ASan 42 s, TSan 2.5 s (`~[gpu]`). Under `ctest -j16`
the slowest test is the existing allocation-failure sweep in the ASan flavor, 65 s. Whole suite: 65 s wall, 1826 tests.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings, 8 fixed, 1 noted.

- **Fixed:** the guide claimed every table row had a test; two rows did not (a pass whose pipeline cannot be built; a mid-frame real-device loss). Added the
  two pipeline-build tests (`max_color_attachments = 2`, `max_bind_groups = 1` on the null device) and reworded the guide to name the
  tests and the one gap (the real backend's injected loss fires only at the start of a frame; in KNOWN-ISSUES).
- **Fixed:** the real-device loss sweep asserted separate lower bounds on "refused" and "lost" runs, which depend on how many frames the scene
  upload takes; it now asserts only that some injected loss took effect.
- **Fixed:** a dead `scheduler_free` variable in the fault test.
- **Fixed:** `DestroyTexture` on a possibly invalid target handle in the Vulkan test helper and in the bench.
- **Fixed:** `BenchLights`/`BenchPool` printed bogus numbers when the scene or a frame failed; they now stop with "NOT OK". Bench arguments are
  validated (`--frames`, `--threads`, `--objects`, `--section`), unknown or bad values print usage and exit 2.
- **Fixed:** two thresholds for revisiting the per-object pipeline lookup (10000 vs 100000 objects); both say 100000.
- **Noted, not changed:** the renderer test binaries compile the demo's frame/scene sources directly instead of linking `renderer_demo_lib`,
  because each sanitizer flavor needs its own objects (and `tests/rhi` does the same); a comment in `tests/renderer/CMakeLists.txt` says so.
- **Process:** `STATUS.md` and this file were updated in the same commit.
