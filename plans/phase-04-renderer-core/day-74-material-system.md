# Phase 4 / Day 74 — The Material System

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 73.

A material is a shader permutation plus parameters and textures; the system owns bind groups and pipelines so the renderer never builds them per draw.

## Tasks

- [ ] `MaterialTemplate` (minted shader permutation + reflection: parameter block layout, texture/sampler slots, render state: cull, depth, blend) and `Material` (an instance: parameter values, texture handles); creation validates values against the reflected block (names, types, sizes) and logs/returns invalid on mismatch.
- [ ] Material bind group (set 1 by convention: set 0 is `FrameData`, set 2 the object buffer): one parameter uniform buffer + textures + samplers per material, rebuilt only when a texture changes; parameter writes go to a CPU shadow and are flushed once per frame in one batch.
- [ ] Pipeline cache keyed by (template, permutation, target formats, vertex layout): lazily created, deduplicated, never created inside a frame's recording (a warm-up call creates what a scene needs); hit/miss counters exposed for tests.
- [ ] Default textures (white, black, flat normal) created by the system; missing texture slots use them.
- [ ] Tests: parameter offsets against the shader's std140 block, a material with a missing or ill-typed value is rejected, two materials of one template share one pipeline, changing a texture rebuilds only that bind group, destruction order with deferred release on the real device.
- [ ] The draw sort key (pipeline, then material, then mesh) is defined and tested; it is what makes bind changes cheap in the G-buffer pass.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Material types and the authoring rules (set/binding convention, parameter naming) are documented in headers and the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Materials and pipelines are created once, deduplicated and validated against reflection; a draw needs no per-draw object creation.
