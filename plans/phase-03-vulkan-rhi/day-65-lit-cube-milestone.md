# Phase 3 / Day 65 — Lit Cube — the Phase 3 Milestone

**Phase:** [Phase 3 — Vulkan RHI](../phase-03-vulkan-rhi.md)
**Depends on:** Day 64.

The Definition of done: a lit, textured cube through Vulkan on Linux, with nothing above the RHI touching Vulkan.

## Tasks

- [ ] Lighting in GLSL: `lit.vert` (world position, normal via the normal matrix passed in the UBO) and `lit.frag` (one directional light, Lambert diffuse + ambient, light direction/color/intensity from a second UBO range or the same block, texture albedo); the maths documented in the shader header.
- [ ] Light parameters changed per frame from the demo (light orbits the cube); camera is a simple orbit controlled by keyboard/mouse via the Phase 1 `InputState` (arrow keys / drag) — exercising the input path with the renderer.
- [ ] `rhi_demo` finalized: command-line options (`--backend`, `--frames`, `--screenshot`, `--no-vsync`, `--validation`), clean exit codes, and a README at `game/rhi_demo/README.md` with the run commands.
- [ ] Tests `[gpu]` offscreen: golden for a fixed light/camera; analytic: a face whose normal points at the light is brighter than a face pointing away by the expected Lambert ratio (±tolerance); with light intensity 0 the image equals the ambient-only expectation.
- [ ] Run the full demo for 1000 frames under validation + sync validation on Wayland and on X11: zero messages, no leaks, stable frame time (no growth in memory: sample RSS at frame 100 and 1000 and assert within a small bound).
- [ ] Record the milestone in `PLAN.md` progress log and `STATUS.md`: screenshot description, frame times, GPU/driver used.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `game/rhi_demo/README.md` and the finished hello-rhi tutorial (all tagged samples compile in `docs_samples`); guide pages "RHI overview" and "Shaders" updated for the lit pipeline.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the
  day's diff, fix/decline each finding with a reason, record it under a
  "Post-implementation code review" section here, and mention it in
  `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, the regenerated `docs/reference/` and the guide page together).

**Done:** the lit, textured, depth-tested cube renders through the RHI on Wayland and X11 with validation clean — the phase's visual milestone is met (the grep gate and hardening follow on Day 66).
