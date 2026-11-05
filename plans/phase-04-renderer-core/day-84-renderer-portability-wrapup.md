# Phase 4 / Day 84 — Portability Proof & Phase 4 Wrap-up

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 83.

Show the renderer and its shaders are not Vulkan in disguise, finish the docs, and close the phase.

## Tasks

- [ ] A `shaders_portability` ctest (skipped without `slangc`) compiles every Slang shader to HLSL and MSL: all must produce output without errors; record clip-space Y behavior and matrix layout per target and add any needed flag to `shaders/README.md`. If `dxc` or a Metal compiler happens to be installed, run it as an optional extra check.
- [ ] Portability review of the renderer in `docs/superpowers/specs/2026-11-04-renderer-portability-review.md`: render graph barriers vs D3D12 enhanced barriers / Metal tracking, the set 0/1/2 convention vs a D3D12 root signature / Metal argument buffers, G-buffer format support, depth sampling (typeless shim), MRT count (4 + depth); a verdict (fits/shim/change) per item; cheap changes made now with tests.
- [ ] Documentation completion: `docs/guide/renderer.md` finished and marked `draft`, `docs/guide/rhi-shaders.md` reviewed, the tutorial runs, reference regenerated, lint baseline zero.
- [ ] Update notes in `plans/phase-05-windows-dx12.md`, `plans/phase-06-macos-metal.md`, `plans/phase-07-font-text-rendering.md` and `plans/phase-08-ui-system.md` with what Phase 4 gives them (graph passes, the material system for glyph atlases, pass ordering) and what they add (an overlay/forward pass after tonemap, since the main pipeline is deferred), and in `plans/phase-09-ecs.md` how the scene list is replaced.
- [ ] Final full run: plain, ASan, TSan, docs, GPU tests with validation + sync validation, both demos on both display backends; results recorded in the day file.
- [ ] Mark Phase 4 complete in `plans/phase-04-renderer-core.md`, `PLAN.md` and `STATUS.md`; Phase 5 (Windows port + DX12) is next and needs its own day files before it starts.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The guide is finished and marked `draft`, and both reviews are linked from `docs/README.md`. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Every shader compiles for all three target languages, the portability review is written with cheap fixes applied, all test flavors pass, and Phase 4 is marked complete.
