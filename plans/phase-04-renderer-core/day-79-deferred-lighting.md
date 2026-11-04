# Phase 4 / Day 79 — Deferred Lighting: Directional & Ambient

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 78.

Turn the G-buffer into lit pixels in linear HDR.

## Tasks

- [ ] Light types in the scene: directional (direction, color, intensity) and ambient; a per-frame `LightData` storage buffer.
- [ ] Slang lighting shader: fullscreen triangle; reconstruct world position from depth + inverse view-projection (undoing exactly the projection's Y flip, verified against known geometry); unpack normal/albedo/material; Lambert diffuse plus a specular term (Blinn-Phong or GGX, chosen in the spec, kept in one function so later phases can change it); write RGBA16_Float to the light accumulation target; depth 1.0 (background) is skipped.
- [ ] Graph wiring: the lighting pass reads G-buffer layers as `Sampled` and depth as `DepthRead`, writes the accumulation target as a color attachment; the graph inserts all barriers.
- [ ] Tonemap/resolve pass (exposure plus a documented operator, then linear to sRGB through the swapchain format) reads the accumulation buffer and writes the swapchain; the operator is unit-tested on the CPU against the shader's constants.
- [ ] Tests: a flat plane with a known albedo and light gives the analytic Lambert value at probe pixels (HDR target read back, tolerance documented); a light-direction sweep matches `max(dot(N,L),0)`; reconstructed world position of probe pixels matches the geometry (catches Y-flip bugs); the Phase 3 lit cube rendered through the deferred path is close to the Phase 3 forward golden (tolerance documented).
- [ ] Validation and sync validation are silent in every test.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Lights, the lighting model, position reconstruction and the tonemap, with exact formulas, are documented in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** A deferred frame with directional and ambient light produces analytically verified HDR output, tonemapped to the swapchain, with validation clean.
