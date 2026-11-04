# Phase 4 / Day 70 — Port the Remaining Shaders & Retire GLSL

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 69.

Finish the migration: the cube, textured and lit shaders in Slang, golden images identical, then delete the GLSL path.

## Tasks

- [ ] Port `cube.vert/frag`, `textured.vert/frag`, `lit.vert/frag` to Slang, keeping the std140 uniform block layouts bit-identical to the `Mat4`/`LitUniforms` C++ structs (their `static_assert`s on sizes must still hold).
- [ ] Switch the default to `ENGINE_SHADER_LANG=slang`; run every golden test (triangle, cube, textured cube, lit cube) and the analytic probes: all pass with the same tolerances and no golden regenerated. If a golden has to change, stop and find the cause - a changed golden means a convention was lost.
- [ ] Run the 1000-frame `rhi_demo --validation` check on Wayland and X11 against the Slang shaders: zero messages, frame time within noise of the Phase 3 numbers.
- [ ] Delete the GLSL sources, the `glslc` branch of `shaders/CMakeLists.txt`, the GLSL handling in `shaders/check.cmake` and the `ENGINE_SHADER_LANG` switch; regenerate the `.inc` files from Slang only; `shaders_check` recompiles and compares with `slangc` (skips with a message on a version mismatch, not registered without `slangc`).
- [ ] Update `PLAN.md` assumption #18 ("GLSL ported, `glslc` removed"), `CLAUDE.md`'s shader bullet and the memory note where they name `glslc` as current.
- [ ] Check the clip-space rule from the Phase 3 review: emit HLSL and MSL (`slangc -target hlsl` / `-target metal`) for the lit shaders into the scratch directory, confirm they are produced without errors, and record whether the Y flip needs a flag there.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `docs/guide/rhi-shaders.md` and `docs/tutorials/hello-rhi.md` describe only Slang (tagged samples still compile and run). `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** No GLSL left in the tree, all goldens pass unchanged, the demo runs clean on both display backends, and the docs describe only the Slang workflow.
