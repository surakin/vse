# Phase 4 / Day 80 — Point Lights & Light Volumes

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 79.

Many local lights without a fullscreen pass per light.

## Tasks

- [ ] `PointLight` (position, radius, color, intensity) with a documented attenuation function that reaches exactly zero at the radius (so the volume is a correct bound); `LightData` holds up to a documented maximum and overflow is an error, not silent truncation.
- [ ] Light volume pass: draw a sphere mesh per visible light, additive blend into the accumulation target, front faces culled so a camera inside a volume still lights, depth test off, scissor to the light's screen bounds; the shader reads the G-buffer as for directional light; lights are frustum-culled with the Day 73 tests and sorted deterministically.
- [ ] Light data path: one storage buffer of lights indexed by instance id; no per-light bind group or pipeline change.
- [ ] Tests: a single point light on a plane matches the analytic falloff at probe pixels; a camera inside a light volume is still lit; zero contribution outside the radius; N overlapping lights sum linearly (tolerance); a culled light issues no draw (counter).
- [ ] Scale measurement (recorded, not asserted): 1, 100 and 1000 lights at 1080p offscreen, frame time and draw counts noted in the day file.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** Point lights, the attenuation formula, limits and the light volume pass are documented in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** Hundreds of point lights light a scene correctly in additive volumes, verified against the analytic model, with limits enforced.
