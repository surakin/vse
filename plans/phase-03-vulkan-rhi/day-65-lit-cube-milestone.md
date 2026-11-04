# Phase 3 / Day 65 — Lit Cube — the Phase 3 Milestone

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 64.

The Definition of done: a lit, textured cube through Vulkan on Linux, with nothing above the RHI touching Vulkan.

## Tasks

- [x] Lighting in GLSL: `lit.vert` (world position, normal via the normal matrix passed in the UBO) and `lit.frag` (one directional light, Lambert diffuse + ambient, light direction/color/intensity from a second UBO range or the same block, texture albedo); the maths documented in the shader header.
- [x] Light parameters changed per frame from the demo (light orbits the cube); camera is a simple orbit controlled by keyboard/mouse via the Phase 1 `InputState` (arrow keys / drag) — exercising the input path with the renderer.
- [x] `rhi_demo` finalized: command-line options (`--backend`, `--frames`, `--screenshot`, `--no-vsync`, `--validation`), clean exit codes, and a README at `game/rhi_demo/README.md` with the run commands.
- [x] Tests `[gpu]` offscreen: golden for a fixed light/camera; analytic: a face whose normal points at the light is brighter than a face pointing away by the expected Lambert ratio (±tolerance); with light intensity 0 the image equals the ambient-only expectation.
- [x] Run the full demo for 1000 frames under validation + sync validation on Wayland and on X11: zero messages, no leaks, stable frame time (no growth in memory: sample RSS at frame 100 and 1000 and assert within a small bound).
- [x] Record the milestone in `PLAN.md` progress log and `STATUS.md`: screenshot description, frame times, GPU/driver used.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `game/rhi_demo/README.md` and the finished hello-rhi tutorial (all tagged samples compile in `docs_samples`); guide pages "RHI overview" and "Shaders" updated for the lit pipeline.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the lit, textured, depth-tested cube renders through the RHI on Wayland and X11 with validation clean — the phase's visual milestone is met (the grep gate and hardening follow on Day 66).

## Result

**The Phase 3 visual milestone is met:** a lit, textured, depth-tested cube renders through the RHI on Wayland and X11 with validation and synchronization validation clean.

Shaders `lit.vert`/`lit.frag` (one directional light, Lambert diffuse + ambient on a texture albedo; one 176-byte std140 uniform block shared by both stages, with the math and interface in each header; `static_assert`s tie the C++ struct to the block) and `CubeRenderer::DrawLit` / `demo::Lighting` / `demo::NormalMatrix` (cofactor matrix divided by the sign of the determinant). `rhi_demo` is finished: an orbit camera from the Phase 1 `InputState` (arrow keys, left-button drag, wheel), a light that circles the cube every eight seconds, options `--backend`, `--frames`, `--screenshot`, `--no-vsync`, `--validation`, `--help`, exit codes 0/1/2, and a built-in memory check (a run of 200+ frames fails if resident memory grows by more than 8 MiB between frame 100 and the end). `game/rhi_demo/README.md` documents it. Tests (`tests/rhi/vulkan_lit_cube_test.cpp`): the normal matrix (non-uniform scale, mirroring, rotation); the three visible faces' brightness equals `ambient + intensity * max(N.L, 0)` to within 2 levels, with the Lambert ratio between two faces checked; faces pointing away from the light show exactly the ambient color, as does zero intensity, as does a zero light direction; the lit, textured cube against `tests/rhi/golden/lit_cube.ppm` (inspected by eye) plus an analytic texel probe. Guide: RHI overview filled in, "The lit pipeline" in `rhi-shaders.md`, tutorial step "Light it", README. Docs 1683/1683.

**Milestone run** (2026-11-04, NVIDIA GeForce RTX 3060, driver 615.78.08, Linux, the dev machine's 240 Hz display): `rhi_demo --backend {wayland,x11} --validation --frames 1000` - zero validation messages (the layer plus synchronization validation), no leak messages at shutdown, 4.10-4.17 ms/frame (vsync-bound; 240 frames per second), resident memory 334704 -> 335032 KiB on Wayland and 310932 -> 311340 KiB on X11 between frame 100 and frame 1000 (about 0.4 MiB). With `--no-vsync` 3000 frames ran in under a second of frames and memory grew by 0.3 MiB. The screenshot shows the checkerboard cube, its top face brightest, the right face dimmest, the light coming from above and to the side.

## Post-implementation code review

`/code-review high` returned 10 findings.

Fixed: (1) `Draw` on a lit renderer and `DrawLit` on another kind assert instead of feeding the shader the wrong block. (2) `NormalMatrix` divides by the sign of the determinant, so a mirroring model keeps its normals outward (test added, shader comment corrected). (3) A zero light direction gives no direct light instead of NaN (test added). (4) `LitUniforms` has `static_assert`s on its size and offsets, and its unused `model` member was removed from the struct and both shaders (176 bytes instead of 240). (5) The fragment shader no longer normalizes the light direction again; the CPU does it once. (6) The golden test's albedo texture and sampler are owned by the scene, so a failed REQUIRE cleans up. (7) The tutorial and CMake no longer say "spinning". (8) The memory check samples at the first frame at or after 100 and warns when it could not run. (9) `dt` is capped at 0.1 s and a minimized window counts toward `--frames`. (10) Day file, `PLAN.md` progress log, `STATUS.md`.

Declined: none.
