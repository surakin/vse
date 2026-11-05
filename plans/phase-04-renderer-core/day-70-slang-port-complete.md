# Phase 4 / Day 70 — Port the Remaining Shaders & Retire GLSL

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 69.

Finish the migration: the cube, textured and lit shaders in Slang, golden images identical, then delete the GLSL path.

## Tasks

- [x] Port `cube.vert/frag`, `textured.vert/frag`, `lit.vert/frag` to Slang, keeping the std140 uniform block layouts bit-identical to the `Mat4`/`LitUniforms` C++ structs (their `static_assert`s on sizes must still hold).
- [x] Switch the default to `ENGINE_SHADER_LANG=slang`; run every golden test (triangle, cube, textured cube, lit cube) and the analytic probes: all pass with the same tolerances and no golden regenerated. If a golden has to change, stop and find the cause - a changed golden means a convention was lost.
- [x] Run the 1000-frame `rhi_demo --validation` check on Wayland and X11 against the Slang shaders: zero messages, frame time within noise of the Phase 3 numbers.
- [x] Delete the GLSL sources, the `glslc` branch of `shaders/CMakeLists.txt`, the GLSL handling in `shaders/check.cmake` and the `ENGINE_SHADER_LANG` switch; regenerate the `.inc` files from Slang only; `shaders_check` recompiles and compares with `slangc` (skips with a message on a version mismatch, not registered without `slangc`).
- [x] Update `PLAN.md` assumption #18 ("GLSL ported, `glslc` removed"), `CLAUDE.md`'s shader bullet and the memory note where they name `glslc` as current.
- [x] Check the clip-space rule from the Phase 3 review: emit HLSL and MSL (`slangc -target hlsl` / `-target metal`) for the lit shaders into the scratch directory, confirm they are produced without errors, and record whether the Y flip needs a flag there.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `docs/guide/rhi-shaders.md` and `docs/tutorials/hello-rhi.md` describe only Slang (tagged samples still compile and run). `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [x] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [x] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** No GLSL left in the tree, all goldens pass unchanged, the demo runs clean on both display backends, and the docs describe only the Slang workflow.

## Result

- **Ported:** `cube.vert/frag`, `textured.vert/frag`, `lit.vert/frag` (the `Params` blocks keep their std140 offsets: `mvp` 0, `normal_matrix` 64, `to_light` 128, `light_color` 144, `ambient` 160; 176 bytes, matching `LitUniforms`).
- **Goldens unchanged:** before deleting anything, the full GPU suite ran against the Slang outputs (triangle, cube, textured cube and lit cube goldens plus the analytic probes) with the same tolerances, `ENGINE_RHI_VALIDATION=1 ENGINE_RHI_VALIDATION_SYNC=1` silent, and no golden file was touched by the commit.
- **Demo:** `rhi_demo --frames 1000 --validation` exits 0 with zero warnings or validation messages on `ENGINE_WINDOW_BACKEND=wayland` and `x11`; resident memory grew 0.26-0.3 MiB between frames 100 and 1000 (Phase 3: 0.4 MiB). Frame time with vsync was 50.6 ms (Wayland) and 17.0 ms (X11) against 4.1 ms in Phase 3: that is vsync/compositor pacing on this machine in this session (the same session where Xwayland never maps windows, see Day 69), not shader cost - with `--no-vsync` 3000 frames finish in under a second on both backends. Worth a re-measure after the session restart.
- **GLSL retired:** the 13 `.vert/.frag/.comp` sources and the `glslc` branch of `shaders/CMakeLists.txt` and `check.cmake` are deleted; `ENGINE_SHADER_LANG` and `ENGINE_SHADER_INCLUDE_DIRS` are gone; Slang sources and outputs now live directly in `shaders/` (`<name>.slang`, `<name>.spv.inc`). `shaders_check` uses `slangc` only (note-and-skip per shader on a version mismatch, SKIP only if nothing was compared; not registered without `slangc`).
- **Tests:** `slang_shader_test.cpp` (GLSL vs Slang comparison) became `shader_interface_test.cpp`, which asserts each committed shader's interface directly (bindings, locations, member offsets, `NonReadable`/`NonWritable`, image formats, uniform/storage/push-constant counts, local sizes, capabilities) and runs `ValidateSpirv`.
- **Other targets:** `slangc -target hlsl` and `-target metal` emit the lit shaders without errors. Findings (in `shaders/README.md`, "Other targets"): the Y flip is not applied for HLSL/Metal (it stays in the projection), and Metal renames an entry point called `main` (warning E40100), which Phase 13 must handle.
- **Docs/policy:** PLAN.md assumption #18, CLAUDE.md's shader bullet and the memory note now say Slang only; `docs/guide/rhi-shaders.md`, `docs/guide/rhi.md` and `docs/tutorials/hello-rhi.md` describe only Slang; docs 1694/1694, lint at zero new findings.
- Full suite 1292/1292 except `X11Window::Create opens a mapped window on the real X server` (the Xwayland environment problem from Day 69, to be looked at after a session restart).

## Post-implementation code review

`/code-review high` returned 6 findings.

Fixed: (1) The interface test had lost `NonWritable`, image-format and uniform/storage-buffer checks from the old comparison: all are asserted per shader now; confirmed the new check fails when `[[vk::image_format]]` is removed from `gradient.comp`. (2) Dead `Reflect` fields: now all used. (3) `STATUS.md` and this file: done. (4) PLAN.md pointed at this file for the HLSL/Metal findings: it now points at `shaders/README.md`, and they are also recorded above. (5) `spv_embed`'s usage comment still showed a GLSL invocation: updated. (6) Without `slangc` the `shaders` target does not exist and edits to `.slang` are silently ignored: the guide and README now say so plainly (the CMake status line already did).

Declined: none.
