# Phase 4 / Day 81 — The Renderer Demo & Depth Correctness

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 80.

The Phase 4 milestone: a scene with many objects, materials and lights and correct depth, running live on Wayland and X11.

## Tasks

- [ ] `game/renderer_demo` (the showcase; `rhi_demo` stays as the RHI dogfood app): at least 500 objects (spheres, cubes, planes), 6+ materials (plain, normal-mapped, varying roughness/metallic), a few hundred point lights plus one directional, orbit and fly cameras through `InputState`, options `--backend/--frames/--screenshot/--validation/--gbuffer LAYER/--lights N/--objects N`, a frame-time line once a second and the resident-memory check from Phase 3.
- [ ] Procedural textures and a small asset set under `game/renderer_demo/assets/` (checker, normal map, roughness map) produced by a documented generator (no binary blob without a way to regenerate it).
- [ ] Golden-image tests of the demo scene at a fixed camera and time (offscreen); depth-correctness tests with objects drawn in several shuffled orders (identical images) and intersecting objects (analytic probes on the intersection line).
- [ ] Milestone run: 1000 frames on Wayland and on X11 with `--validation` (sync validation on): zero messages; frame time and memory recorded in the day file with GPU, driver and settings.
- [ ] Exit codes and failure behavior match `rhi_demo` (device lost = exit 3 with a message); the `demo`-label ctests cover `--screenshot` and fault injection.
- [ ] `game/renderer_demo/README.md` and the tutorial `docs/tutorials/hello-renderer.md` (scene -> material -> light -> frame; tagged samples that run on the null device).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The tutorial is written and the guide's usage sections are finished. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** The renderer demo shows a large lit scene with correct depth on both display backends, validation clean for 1000 frames, goldens and probes passing.
