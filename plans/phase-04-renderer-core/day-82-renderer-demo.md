# Phase 4 / Day 82 — The Renderer Demo & Depth Correctness

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 81.

The Phase 4 milestone: a scene with many objects, materials and lights and correct depth, running live on Wayland and X11.

## Tasks

- [x] `game/renderer_demo` (the showcase; `rhi_demo` stays as the RHI dogfood app): at least 500 objects (spheres, cubes, planes), 6+ materials (plain, normal-mapped, varying roughness/metallic), a few hundred point lights plus one directional, orbit and fly cameras through `InputState`, options `--backend/--frames/--screenshot/--validation/--gbuffer LAYER/--lights N/--objects N`, a frame-time line once a second and the resident-memory check from Phase 3.
- [x] Procedural textures and a small asset set under `game/renderer_demo/assets/` (checker, normal map, roughness map) produced by a documented generator (no binary blob without a way to regenerate it).
- [x] Golden-image tests of the demo scene at a fixed camera and time (offscreen); depth-correctness tests with objects drawn in several shuffled orders (identical images) and intersecting objects (analytic probes on the intersection line).
- [x] Milestone run: 1000 frames on Wayland and on X11 with `--validation` (sync validation on): zero messages; frame time and memory recorded in the day file with GPU, driver and settings.
- [x] Exit codes and failure behavior match `rhi_demo` (device lost = exit 3 with a message); the `demo`-label ctests cover `--screenshot` and fault injection.
- [x] `game/renderer_demo/README.md` and the tutorial `docs/tutorials/hello-renderer.md` (scene -> material -> light -> frame; tagged samples that run on the null device).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The tutorial is written and the guide's usage sections are finished. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

## Result

- `game/renderer_demo` (library `renderer_demo_lib` for the scene, the frame and the assets; executables `renderer_demo` and `renderer_demo_mint`): 540 objects by default (49 ground tiles, spheres, cubes and a vertex-colored sphere on a grid), eight materials (plain, checker and stripe albedo textures, normal map, smooth gold, rough copper, emissive, per-vertex colors), 300 point lights plus a sun and ambient light, orbit and fly cameras through `InputState` (Tab switches), options `--backend --frames --screenshot --validation --gbuffer LAYER --lights N --objects N --no-vsync --inject-*`, a frame-time line once a second (objects, draw calls, lights written, culled and dropped, volume draws) and the Phase 3 resident-memory check. It runs through `renderer::Renderer::RenderFrame` and the render graph (G-buffer, lighting with light volumes, tonemap).
- Assets: `assets/{checker,stripes,normal}.rtex`, written by `renderer_demo_mint` from `demo_assets.cpp`; ctest `renderer_demo_assets_current` writes them twice (deterministic) and compares with the committed files byte for byte, and fails on a file in `assets/` that the minter does not write.
- Tests (`tests/rhi/vulkan_renderer_demo_test.cpp`, validation and sync validation silent): the demo scene at a fixed camera and time against the golden `tests/rhi/golden/renderer_demo.ppm` (320x180, tolerance 6, with independent checks for the object count, instanced draws, light volume draws equal to the lights written, black sky, lit ground); the same scene added in three shuffled orders gives the same pixels (exactly zero differing pixels); two crossing planes (floor and wall) show the right surface on both sides of the intersection line and the occluded halves hidden, at 40+ probes with analytic depth, identical in either draw order; the bump normal map is unit-length and tilts as a sphere of radius 0.9 would; the scene's positions are fixed numbers for a seed (catches compiler-dependent generation). Mutations: with the depth compare set to `Always` the order test and the crossing-planes test fail (61 assertions); with the old bump radius the normal-map test fails.
- Demo ctests (`-L demo`, 16 with the asset check): screenshot with validation, every G-buffer layer, device loss at frame 3 (exit 3 and the message), allocation failures at the 1st and 3rd allocation (exit 1, no crash), bad options (exit 2), no Vulkan loader. Each test has its own output file and pipeline cache.
- Docs: `game/renderer_demo/README.md`, the tutorial `docs/tutorials/hello-renderer.md` (scene, material, light, frame; the tagged sample runs on the null device and prints `frames ok: 3, directional: 1, point: 1, point draws: 1, errors: 0`), a short "The renderer demo" section in the renderer guide and the tutorials index. `docgen lint` 2451/2451, docs regenerated, `ctest -L docs` passes.
- **Milestone run (2026-11-05):** `renderer_demo --frames 1000 --validation` (validation layer plus synchronization validation), 540 objects, 300 point lights, 1280x720 window, on an NVIDIA GeForce RTX 3060 (driver 615.78.08, Vulkan instance 1.4.363) in a Wayland session. Wayland backend with `--no-vsync`: exit 0, zero validation messages, 1.27 ms/frame (791 frames in the last second of logging), 487 objects drawn in 16 draws, 297 volume draws (3 lights culled, 0 dropped), resident memory 310148 KiB at frame 100 and 310324 KiB at frame 1000. X11 backend (the session's `DISPLAY=:0`, served by the compositor's X server) with `--no-vsync`: exit 0, zero messages, 1.28 ms/frame, resident 319944 KiB at frame 100 and 320136 KiB at frame 1000. Wayland with vsync, 600 frames: 4.1 ms/frame (the display's pace), memory 310204 to 310404 KiB, zero messages.
- Deviations from the task text: (1) no roughness map. The G-buffer fragment shader has no roughness texture input (its permutations are normal map, alpha test and vertex color), so adding one means changing the shader and the material layout; the varying roughness and metallic come from the material parameters (gold at 0.2, copper at 0.6, plastic at 0.5, ...) and a stripe albedo texture takes its place in the asset set. Filed in `KNOWN-ISSUES.md`. (2) The demo does not use the job scheduler for parallel recording (`RendererDesc::scheduler` is null): a frame is three passes, and the milestone frame time is already about 1.3 ms. (3) The screenshot is 640x360 and the golden 320x180 to keep the committed file small.

## Post-implementation code review

`/code-review high`: 10 findings.

- **Fixed:** several `random.Range()` calls were arguments of one `Vec3` constructor, so the order of evaluation (unspecified) could change the scene between compilers and break the golden. Each value is now drawn in its own statement; a test pins the generated positions for a seed.
- **Fixed:** the bump normal map divided by 0.81 (the radius squared) where the radius 0.9 was needed, tilting every bump about 11% too much. Re-minted `normal.rtex` and re-blessed the golden. Test: unit length everywhere and the normal at a known texel; it fails with the old constant.
- **Fixed:** `DemoFrame::Create` did not check `GBufferDebugView::Valid()`; a failure there ran into a null pipeline at frame time. Checked now.
- **Fixed:** `SetupFor` in `main.cpp` was dead and disagreed with the two hand-built setups; both run functions use it now.
- **Fixed:** leftover unused locals and `(void)` lines and a wrong comment in the test; removed and corrected.
- **Fixed:** `std::string` in the demo library and the minter replaced by `snprintf` into a fixed buffer (the test and the older `game/common/textures.cpp` still use `std::vector`; `rhi_demo` does too, and tests may use the STL).
- **Fixed:** the asset check only looked at files the minter writes; it now also fails on any file in `assets/` that the minter does not write.
- **Fixed:** all demo ctests wrote one `fault.ppm` and shared one pipeline cache directory, a race under `ctest -j`; each test has its own file and cache.
- **Fixed:** `DemoFrame::Destroy` left stale templates and statistics and did not idle the device; it now idles first and resets them.
- **Process:** this file, `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; the milestone run is recorded above.

**Done:** The renderer demo shows a large lit scene with correct depth on both display backends, validation clean for 1000 frames, goldens and probes passing.
